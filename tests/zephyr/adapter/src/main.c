/* SPDX-License-Identifier: Apache-2.0 */
#include <embedids/zephyr.h>
#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>
#include <errno.h>
#include <string.h>

_Static_assert(EMBEDIDS_VERSION_MAJOR == 0, "Generated version must be numeric");
_Static_assert(EMBEDIDS_MAX_METRICS == 2, "Kconfig must configure consumers too");

static struct embedids_zephyr service;
static embedids_metric_config_t metrics[2];
static embedids_metric_datapoint_t history[2][3];
static embedids_system_config_t config;
static uint32_t callbacks;
static uint32_t anomalies;
static uint32_t custom_calls;

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(&service, 0, sizeof(service));
	memset(metrics, 0, sizeof(metrics));
	memset(history, 0, sizeof(history));
	config = (embedids_system_config_t){
		.metrics = metrics, .max_metrics = 2, .num_active_metrics = 2
	};
	for (int i = 0; i < 2; ++i) {
		strcpy(metrics[i].metric.name, i ? "other" : "usage");
		metrics[i].metric.type = EMBEDIDS_METRIC_TYPE_UINT32;
		metrics[i].metric.history = history[i];
		metrics[i].metric.max_history_size = 3;
		metrics[i].metric.enabled = true;
		metrics[i].num_algorithms = i ? 1 : 2;
		metrics[i].algorithms[0] = (embedids_algorithm_t){
			.type = EMBEDIDS_ALGORITHM_THRESHOLD, .enabled = true,
			.config.threshold = {.check_max = true, .max_threshold.u32 = 10}
		};
	}
	metrics[0].algorithms[1] = (embedids_algorithm_t){
		.type = EMBEDIDS_ALGORITHM_THRESHOLD, .enabled = true,
		.config.threshold = {.check_min = true, .min_threshold.u32 = 2}
	};
	callbacks = anomalies = custom_calls = 0;
	zassert_equal(embedids_zephyr_init(&service, &config), EMBEDIDS_OK);
}

static void result(uint16_t metric, uint32_t algorithm, embedids_result_t status, void *user)
{
	ARG_UNUSED(user);
	zassert_true(metric < 2);
	zassert_true(algorithm < metrics[metric].num_algorithms);
	callbacks++;
	anomalies += status != EMBEDIDS_OK;
}

static void submit(uint32_t value)
{
	zassert_ok(embedids_zephyr_submit(&service, 0, (embedids_metric_value_t){.u32 = value}));
}

ZTEST(adapter, test_each_sample_is_analyzed)
{
	submit(11);
	submit(5);
	zassert_equal(embedids_zephyr_poll(&service, result, NULL), EMBEDIDS_OK);
	zassert_equal(callbacks, 4);
	zassert_equal(anomalies, 1, "Transient anomaly must not be overwritten by latest value");
	zassert_equal(metrics[0].metric.current_size, 2);
	zassert_equal(embedids_zephyr_poll(&service, result, NULL), EMBEDIDS_OK);
	zassert_equal(callbacks, 4, "No duplicate evaluation without a new sample");
}

ZTEST(adapter, test_overflow_is_counted_and_bounded)
{
	for (int i = 0; i < CONFIG_EMBEDIDS_SAMPLE_QUEUE_SIZE; ++i) { submit(5); }
	zassert_equal(embedids_zephyr_submit(&service, 0,
					    (embedids_metric_value_t){.u32 = 5}), -ENOMSG);
	zassert_equal(embedids_zephyr_dropped(&service), 1);
	zassert_equal(embedids_zephyr_poll(&service, result, NULL), EMBEDIDS_OK);
	zassert_equal(callbacks, 2 * CONFIG_EMBEDIDS_SAMPLE_QUEUE_SIZE);
	zassert_equal(metrics[0].metric.current_size, 3);
	zassert_equal(metrics[0].metric.write_index, CONFIG_EMBEDIDS_SAMPLE_QUEUE_SIZE % 3);
	submit(5);
}

ZTEST(adapter, test_independent_metrics_and_history_wrap)
{
	for (int i = 0; i < 8; ++i) {
		submit(5);
		zassert_equal(embedids_zephyr_poll(&service, result, NULL), EMBEDIDS_OK);
	}
	zassert_equal(metrics[0].metric.write_index, 2);
	zassert_equal(metrics[1].metric.current_size, 0);
	zassert_ok(embedids_zephyr_submit(&service, 1, (embedids_metric_value_t){.u32 = 20}));
	zassert_equal(embedids_zephyr_poll(&service, result, NULL), EMBEDIDS_OK);
	zassert_equal(anomalies, 1);
	zassert_equal(metrics[1].metric.current_size, 1);
}

ZTEST(adapter, test_invalid_producer_inputs)
{
	zassert_equal(embedids_zephyr_submit(NULL, 0, (embedids_metric_value_t){0}), -EINVAL);
	zassert_equal(embedids_zephyr_submit(&service, 2, (embedids_metric_value_t){0}), -EINVAL);
	metrics[1].metric.enabled = false;
	zassert_equal(embedids_zephyr_submit(&service, 1, (embedids_metric_value_t){0}), -EINVAL);
	zassert_equal(embedids_zephyr_dropped(&service), 0);
	zassert_equal(embedids_zephyr_poll(NULL, NULL, NULL), EMBEDIDS_ERROR_NOT_INITIALIZED);
}

ZTEST(adapter, test_invalid_configuration)
{
	zassert_equal(embedids_zephyr_init(NULL, &config), EMBEDIDS_ERROR_INVALID_PARAM);
	zassert_equal(embedids_zephyr_init(&service, NULL), EMBEDIDS_ERROR_INVALID_PARAM);
	config.num_active_metrics = 3;
	zassert_equal(embedids_zephyr_init(&service, &config), EMBEDIDS_ERROR_CONFIG_INVALID);
	config.num_active_metrics = 2;
	metrics[0].metric.max_history_size = 0;
	zassert_equal(embedids_zephyr_init(&service, &config), EMBEDIDS_ERROR_CONFIG_INVALID);
	metrics[0].metric.max_history_size = 3;
	metrics[0].metric.write_index = 3;
	zassert_equal(embedids_zephyr_init(&service, &config), EMBEDIDS_ERROR_CONFIG_INVALID);
	metrics[0].metric.write_index = 0;
	metrics[0].num_algorithms = 3;
	zassert_equal(embedids_zephyr_init(&service, &config), EMBEDIDS_ERROR_CONFIG_INVALID);
	metrics[0].num_algorithms = 2;
	strcpy(metrics[1].metric.name, "usage");
	zassert_equal(embedids_zephyr_init(&service, &config), EMBEDIDS_ERROR_CONFIG_INVALID);
	strcpy(metrics[1].metric.name, "other");
	memset(metrics[0].metric.name, 'x', sizeof(metrics[0].metric.name));
	zassert_equal(embedids_zephyr_init(&service, &config), EMBEDIDS_ERROR_CONFIG_INVALID);
}

ZTEST(adapter, test_unsupported_algorithms_rejected)
{
	metrics[0].algorithms[0].type = EMBEDIDS_ALGORITHM_TREND;
	zassert_equal(embedids_zephyr_init(&service, &config), EMBEDIDS_ERROR_ALGORITHM_NOT_SUPPORTED);
	metrics[0].algorithms[0].type = EMBEDIDS_ALGORITHM_THRESHOLD;
	metrics[0].metric.type = EMBEDIDS_METRIC_TYPE_BOOL;
	zassert_equal(embedids_zephyr_init(&service, &config), EMBEDIDS_ERROR_ALGORITHM_NOT_SUPPORTED);
	metrics[0].metric.type = EMBEDIDS_METRIC_TYPE_UINT32;
	metrics[0].algorithms[0].type = EMBEDIDS_ALGORITHM_CUSTOM;
	metrics[0].algorithms[0].config.custom.function = NULL;
	zassert_equal(embedids_zephyr_init(&service, &config), EMBEDIDS_ERROR_CUSTOM_ALGORITHM_NULL);
}

static embedids_result_t custom(const embedids_metric_t *metric, const void *cfg, void *context)
{
	ARG_UNUSED(cfg);
	zassert_equal(metric->current_size, 1);
	zassert_equal(context, &custom_calls);
	custom_calls++;
	return EMBEDIDS_ERROR_CUSTOM_DETECTION;
}

ZTEST(adapter, test_custom_algorithm_runs_after_threshold_anomaly)
{
	metrics[0].algorithms[1] = (embedids_algorithm_t){
		.type = EMBEDIDS_ALGORITHM_CUSTOM, .enabled = true,
		.config.custom = {.function = custom, .context = &custom_calls}
	};
	submit(20);
	zassert_equal(embedids_zephyr_poll(&service, result, NULL), EMBEDIDS_OK);
	zassert_equal(custom_calls, 1);
	zassert_equal(anomalies, 2);
}

static void isr_submit(const void *unused)
{
	ARG_UNUSED(unused);
	submit(5);
}

ZTEST(adapter, test_isr_producer)
{
	irq_offload(isr_submit, NULL);
	zassert_equal(embedids_zephyr_poll(&service, result, NULL), EMBEDIDS_OK);
	zassert_equal(callbacks, 2);
	zassert_equal(anomalies, 0);
}

static struct k_thread producer_threads[2];
K_THREAD_STACK_ARRAY_DEFINE(producer_stacks, 2, 1024);

static void concurrent_producer(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	for (int i = 0; i < 16; ++i) {
		int status = embedids_zephyr_submit(&service, 0,
						   (embedids_metric_value_t){.u32 = 5});
		zassert_true(status == 0 || status == -ENOMSG);
	}
}

ZTEST(adapter, test_concurrent_producers)
{
	for (int i = 0; i < 2; ++i) {
		k_thread_create(&producer_threads[i], producer_stacks[i],
				K_THREAD_STACK_SIZEOF(producer_stacks[i]), concurrent_producer,
				NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	}
	for (int i = 0; i < 2; ++i) {
		zassert_ok(k_thread_join(&producer_threads[i], K_FOREVER));
	}
	zassert_equal(embedids_zephyr_dropped(&service), 32 - CONFIG_EMBEDIDS_SAMPLE_QUEUE_SIZE);
	zassert_equal(embedids_zephyr_poll(&service, result, NULL), EMBEDIDS_OK);
	zassert_equal(callbacks, 2 * CONFIG_EMBEDIDS_SAMPLE_QUEUE_SIZE);
}

#if EMBEDIDS_ENABLE_FLOATING_POINT
ZTEST(adapter, test_float_and_double_configurations)
{
	metrics[0].num_algorithms = 1;
	metrics[0].metric.type = EMBEDIDS_METRIC_TYPE_FLOAT;
	metrics[0].algorithms[0].config.threshold.max_threshold.f32 = 10.0f;
	zassert_ok(embedids_zephyr_submit(&service, 0, (embedids_metric_value_t){.f32 = 11.0f}));
	zassert_equal(embedids_zephyr_poll(&service, result, NULL), EMBEDIDS_OK);
	zassert_equal(anomalies, 1);
#if EMBEDIDS_ENABLE_DOUBLE_PRECISION
	metrics[1].metric.type = EMBEDIDS_METRIC_TYPE_DOUBLE;
	metrics[1].algorithms[0].config.threshold.max_threshold.f64 = 10.0;
	zassert_ok(embedids_zephyr_submit(&service, 1, (embedids_metric_value_t){.f64 = 11.0}));
	zassert_equal(embedids_zephyr_poll(&service, result, NULL), EMBEDIDS_OK);
	zassert_equal(anomalies, 2);
#endif
}
#endif

ZTEST_SUITE(adapter, NULL, NULL, before, NULL, NULL);
