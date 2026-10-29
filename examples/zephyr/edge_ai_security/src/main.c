/* SPDX-License-Identifier: Apache-2.0 */
#include <embedids/zephyr.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <limits.h>
#include <string.h>

#define SAMPLE_MS 100
#define HEARTBEAT_TIMEOUT_MS 500
#define HISTORY_SIZE 4
#define INFERENCE_QUEUE_SIZE 8

enum metric_id {
	STATE_ABUSE, REQUEST_RATE, QUEUE_REJECTS, CONFIG_MISMATCH,
	UNKNOWN_DESTINATION, TX_VOLUME, HEARTBEAT_AGE, TELEMETRY_DROPS, METRIC_COUNT
};

static const char *const names[METRIC_COUNT] = {
	"state_abuse", "request_rate", "queue_rejects", "config_integrity",
	"unknown_destination", "tx_volume", "heartbeat_age", "telemetry_drops"
};
static const uint32_t limits[METRIC_COUNT] = {
	0, 32, 0, 0, 0, 4096, HEARTBEAT_TIMEOUT_MS, 0
};

static struct embedids_zephyr detector;
static embedids_metric_datapoint_t history[METRIC_COUNT][HISTORY_SIZE];
static embedids_metric_config_t metrics[METRIC_COUNT];
static embedids_system_config_t system_config = {
	.metrics = metrics, .max_metrics = METRIC_COUNT, .num_active_metrics = METRIC_COUNT
};

K_MUTEX_DEFINE(state_lock);
K_MUTEX_DEFINE(output_lock);
K_SEM_DEFINE(started, 0, 2);
K_MSGQ_DEFINE(inference_queue, sizeof(uint32_t), INFERENCE_QUEUE_SIZE, 4);

/* All fields are protected by state_lock. Cumulative uint32 counters use
 * modulo subtraction at sampling time; command sizes are bounded well below
 * one full counter wrap per interval.
 */
static struct {
	bool active;
	bool heartbeat_enabled;
	uint32_t work_units;
	uint32_t expected_work_units;
	uint32_t state_abuse;
	uint32_t requests;
	uint32_t rejects;
	uint32_t unknown_destinations;
	uint32_t tx_bytes;
	uint32_t completed;
	uint32_t last_result;
	uint64_t heartbeat_ms;
} device;

static bool latched[METRIC_COUNT]; /* detector thread only */
static uint32_t max_detection_us;  /* detector thread only; snapshot below */
static uint32_t reported_detection_us; /* protected by state_lock */

static void command_result_at(const char *name, bool ok, uint64_t trigger_ms)
{
	k_mutex_lock(&output_lock, K_FOREVER);
	printk("{\"type\":\"command\",\"command\":\"%s\",\"ok\":%s,"
	       "\"timestamp_ms\":%llu,\"trigger_ms\":%llu}\n",
	       name, ok ? "true" : "false", (unsigned long long)k_uptime_get(),
	       (unsigned long long)trigger_ms);
	k_mutex_unlock(&output_lock);
}

static void command_result(const char *name, bool ok)
{
	command_result_at(name, ok, (uint64_t)k_uptime_get());
}

static void detection_result(uint16_t metric, uint32_t algorithm,
			     embedids_result_t result, void *user)
{
	ARG_UNUSED(algorithm);
	ARG_UNUSED(user);
	bool anomalous = result != EMBEDIDS_OK;

	if (anomalous != latched[metric]) {
		const embedids_metric_t *m = &metrics[metric].metric;
		uint32_t index = (m->write_index + m->max_history_size - 1) % m->max_history_size;

		k_mutex_lock(&output_lock, K_FOREVER);
		printk("{\"type\":\"%s\",\"rule\":\"%s\",\"value\":%u,\"limit\":%u,"
		       "\"timestamp_ms\":%llu,\"sample_ms\":%d}\n",
		       anomalous ? "alert" : "clear", names[metric],
		       m->history[index].value.u32, limits[metric],
		       (unsigned long long)m->history[index].timestamp_ms, SAMPLE_MS);
		k_mutex_unlock(&output_lock);
		latched[metric] = anomalous;
	}
}

static void inference_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	k_sem_take(&started, K_FOREVER);
	while (true) {
		uint32_t request;

		k_msgq_get(&inference_queue, &request, K_FOREVER);
		k_mutex_lock(&state_lock, K_FOREVER);
		uint32_t units = device.work_units;
		k_mutex_unlock(&state_lock);
		/* Deterministic synthetic computation, not a neural network. The
		 * delay models service time without burning host CPU continuously.
		 */
		uint32_t result = request;
		for (uint32_t i = 0; i < units * 64U; ++i) {
			result = result * 1664525U + 1013904223U;
		}
		k_sleep(K_MSEC(30));
		k_mutex_lock(&state_lock, K_FOREVER);
		device.completed++;
		device.last_result = result;
		k_mutex_unlock(&state_lock);
	}
}

static void monitor(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	k_sem_take(&started, K_FOREVER);
	uint32_t previous[METRIC_COUNT] = {0};

	while (true) {
		k_sleep(K_MSEC(SAMPLE_MS));
		uint32_t values[METRIC_COUNT];

		k_mutex_lock(&state_lock, K_FOREVER);
		uint64_t now = (uint64_t)k_uptime_get();
		values[STATE_ABUSE] = device.state_abuse;
		values[REQUEST_RATE] = device.requests;
		values[QUEUE_REJECTS] = device.rejects;
		values[CONFIG_MISMATCH] = device.work_units != device.expected_work_units;
		values[UNKNOWN_DESTINATION] = device.unknown_destinations;
		values[TX_VOLUME] = device.tx_bytes;
		uint64_t age = now - device.heartbeat_ms;
		values[HEARTBEAT_AGE] = age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
		k_mutex_unlock(&state_lock);
		values[TELEMETRY_DROPS] = embedids_zephyr_dropped(&detector);

		uint32_t begin = k_cycle_get_32();
		for (uint16_t i = 0; i < METRIC_COUNT; ++i) {
			bool gauge = i == CONFIG_MISMATCH || i == HEARTBEAT_AGE;
			uint32_t value = gauge ? values[i] : values[i] - previous[i];

			previous[i] = values[i];
			/* Queue-full failures are counted inside the adapter. */
			(void)embedids_zephyr_submit(&detector, i,
						     (embedids_metric_value_t){.u32 = value});
		}
		embedids_result_t result = embedids_zephyr_poll(&detector, detection_result, NULL);
		__ASSERT(result == EMBEDIDS_OK, "Detector ingestion failed");
		uint32_t elapsed = k_cyc_to_us_floor32(k_cycle_get_32() - begin);

		max_detection_us = MAX(max_detection_us, elapsed);
		k_mutex_lock(&state_lock, K_FOREVER);
		reported_detection_us = max_detection_us;
		k_mutex_unlock(&state_lock);
	}
}

static void producer(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	/* State is initialized before this thread is started from main. */
	while (true) {
		k_mutex_lock(&state_lock, K_FOREVER);
		if (device.heartbeat_enabled) {
			device.heartbeat_ms = (uint64_t)k_uptime_get();
		}
		k_mutex_unlock(&state_lock);
		k_sleep(K_MSEC(50));
	}
}

K_THREAD_DEFINE(worker_tid, 2048, inference_worker, NULL, NULL, NULL, 6, 0, 0);
K_THREAD_DEFINE(monitor_tid, 2048, monitor, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(producer_tid, 1024, producer, NULL, NULL, NULL, 6, 0, SYS_FOREVER_MS);

static bool number(const char *text, uint32_t maximum, uint32_t *value)
{
	if (!text || !*text) {
		return false;
	}
	*value = 0;
	for (; *text; ++text) {
		if (*text < '0' || *text > '9') {
			return false;
		}
		uint32_t digit = (uint32_t)(*text - '0');
		if (*value > maximum / 10 ||
		    (*value == maximum / 10 && digit > maximum % 10)) {
			return false;
		}
		*value = *value * 10 + digit;
	}
	return true;
}

static void status(void)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	uint32_t completed = device.completed;
	uint32_t result = device.last_result;
	uint32_t detection_us = reported_detection_us;
	bool active = device.active;
	k_mutex_unlock(&state_lock);
	k_mutex_lock(&output_lock, K_FOREVER);
	printk("{\"type\":\"status\",\"active\":%s,\"completed\":%u,\"last_result\":%u,"
	       "\"queued\":%u,\"dropped\":%u,\"max_detection_sim_us\":%u,"
	       "\"history_bytes\":%u,\"adapter_bytes\":%u}\n",
	       active ? "true" : "false", completed, result,
	       k_msgq_num_used_get(&inference_queue), embedids_zephyr_dropped(&detector),
	       detection_us, (unsigned int)sizeof(history), (unsigned int)sizeof(detector));
	k_mutex_unlock(&output_lock);
}

static void handle_command(char *line)
{
	char *args[4];
	size_t count = 0;
	char *p = line;

	while (*p) {
		while (*p == ' ' || *p == '\t') { ++p; }
		if (!*p) { break; }
		if (count == ARRAY_SIZE(args)) {
			command_result("invalid", false);
			return;
		}
		args[count++] = p;
		while (*p && *p != ' ' && *p != '\t') { ++p; }
		if (*p) { *p++ = '\0'; }
	}
	if (!count) { return; }
	uint32_t value;

	if (count == 1 && !strcmp(args[0], "status")) {
		status();
		return;
	}
	if (count == 2 && !strcmp(args[0], "mode") &&
	    (!strcmp(args[1], "active") || !strcmp(args[1], "idle"))) {
		k_mutex_lock(&state_lock, K_FOREVER);
		device.active = !strcmp(args[1], "active");
		k_mutex_unlock(&state_lock);
		command_result("mode", true);
		return;
	}
	if (count == 2 && !strcmp(args[0], "infer") && number(args[1], 256, &value) && value) {
		bool ok = true;
		uint64_t trigger_ms = (uint64_t)k_uptime_get();

		for (uint32_t i = 0; i < value; ++i) {
			k_mutex_lock(&state_lock, K_FOREVER);
			device.requests++;
			if (!device.active) {
				device.state_abuse++;
				ok = false;
			} else if (k_msgq_put(&inference_queue, &i, K_NO_WAIT)) {
				device.rejects++;
				ok = false;
			}
			k_mutex_unlock(&state_lock);
		}
		command_result_at("infer", ok, trigger_ms);
		return;
	}
	if (count == 2 && !strcmp(args[0], "config") && number(args[1], 100, &value) && value) {
		k_mutex_lock(&state_lock, K_FOREVER);
		uint64_t trigger_ms = (uint64_t)k_uptime_get();
		bool ok = !device.active;
		if (ok) {
			device.work_units = value;
			device.expected_work_units = value;
		} else {
			device.state_abuse++;
		}
		k_mutex_unlock(&state_lock);
		command_result_at("config", ok, trigger_ms);
		return;
	}
	if (count == 3 && !strcmp(args[0], "send") &&
	    (!strcmp(args[1], "approved") || !strcmp(args[1], "unknown")) &&
	    number(args[2], 1048576, &value) && value) {
		k_mutex_lock(&state_lock, K_FOREVER);
		uint64_t trigger_ms = (uint64_t)k_uptime_get();
		bool ok = !strcmp(args[1], "approved");
		if (ok) { device.tx_bytes += value; }
		else { device.unknown_destinations++; }
		k_mutex_unlock(&state_lock);
		command_result_at("send", ok, trigger_ms);
		return;
	}
	if (IS_ENABLED(CONFIG_EMBEDIDS_DEMO_FAULT_INJECTION) && count == 2 &&
	    !strcmp(args[0], "fault")) {
		bool ok = true;
		k_mutex_lock(&state_lock, K_FOREVER);
		uint64_t trigger_ms = (uint64_t)k_uptime_get();
		if (!strcmp(args[1], "config")) {
			device.work_units = device.expected_work_units == 100 ? 99 : 100;
		} else if (!strcmp(args[1], "heartbeat")) {
			device.heartbeat_enabled = false;
			trigger_ms = device.heartbeat_ms + HEARTBEAT_TIMEOUT_MS + 1;
		} else if (!strcmp(args[1], "overflow")) {
			for (int i = 0; i < 4 * CONFIG_EMBEDIDS_SAMPLE_QUEUE_SIZE; ++i) {
				(void)embedids_zephyr_submit(&detector, TELEMETRY_DROPS,
							     (embedids_metric_value_t){.u32 = 0});
			}
		} else { ok = false; }
		k_mutex_unlock(&state_lock);
		command_result_at("fault", ok, trigger_ms);
		return;
	}
	command_result("invalid", false);
}

int main(void)
{
	for (uint16_t i = 0; i < METRIC_COUNT; ++i) {
		strcpy(metrics[i].metric.name, names[i]);
		metrics[i].metric.type = EMBEDIDS_METRIC_TYPE_UINT32;
		metrics[i].metric.history = history[i];
		metrics[i].metric.max_history_size = HISTORY_SIZE;
		metrics[i].metric.enabled = true;
		metrics[i].num_algorithms = 1;
		metrics[i].algorithms[0] = (embedids_algorithm_t){
			.type = EMBEDIDS_ALGORITHM_THRESHOLD, .enabled = true,
			.config.threshold = {.max_threshold.u32 = limits[i], .check_max = true}
		};
	}
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	if (embedids_zephyr_init(&detector, &system_config) != EMBEDIDS_OK ||
	    !device_is_ready(uart)) {
		printk("{\"type\":\"fatal\",\"reason\":\"initialization\"}\n");
		return 1;
	}
	device.work_units = device.expected_work_units = 10;
	device.heartbeat_enabled = true;
	device.heartbeat_ms = (uint64_t)k_uptime_get();
	k_thread_start(producer_tid);
	k_sem_give(&started);
	k_sem_give(&started);
	k_mutex_lock(&output_lock, K_FOREVER);
	printk("{\"type\":\"ready\",\"version\":\"%s\",\"sample_ms\":%d,"
	       "\"heartbeat_timeout_ms\":%d,\"fault_injection\":%s}\n",
	       embedids_get_version(), SAMPLE_MS, HEARTBEAT_TIMEOUT_MS,
	       IS_ENABLED(CONFIG_EMBEDIDS_DEMO_FAULT_INJECTION) ? "true" : "false");
	k_mutex_unlock(&output_lock);

	char line[96];
	size_t used = 0;
	bool invalid = false;
	while (true) {
		unsigned char ch;
		if (uart_poll_in(uart, &ch)) { k_sleep(K_MSEC(1)); continue; }
		if (ch == '\r' || ch == '\n') {
			line[used] = '\0';
			if (invalid) { command_result("invalid", false); }
			else { handle_command(line); }
			used = 0;
			invalid = false;
		} else if (ch < 32 || ch > 126 || used == sizeof(line) - 1) {
			invalid = true;
		} else if (!invalid) { line[used++] = (char)ch; }
	}
	return 0;
}
