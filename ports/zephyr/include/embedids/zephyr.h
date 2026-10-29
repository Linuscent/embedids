/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EMBEDIDS_ZEPHYR_H
#define EMBEDIDS_ZEPHYR_H

#include <embedids.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

struct embedids_zephyr_sample {
	uint64_t timestamp_ms;
	embedids_metric_value_t value;
	uint16_t metric_index;
};

/* Allocate statically. Initialize before producers start; never reinitialize
 * while producers or the consumer are active. Configuration must outlive this
 * object. Only the consumer may access the core, histories and configuration.
 */
struct embedids_zephyr {
	embedids_context_t core;
	struct k_msgq samples;
	atomic_t dropped;
	char queue_buffer[CONFIG_EMBEDIDS_SAMPLE_QUEUE_SIZE *
			  sizeof(struct embedids_zephyr_sample)] __aligned(8);
};

typedef void (*embedids_zephyr_result_fn)(uint16_t metric_index,
					uint32_t algorithm_index,
					embedids_result_t result, void *user);

/* Adds strict structural validation; rejects the unfinished trend detector and
 * Boolean thresholds (which currently do not compare values in the C core).
 */
embedids_result_t embedids_zephyr_init(struct embedids_zephyr *service,
				     embedids_system_config_t *config);

/* Thread/ISR-safe, nonblocking. -ENOMSG means queue full (counted as a drop).
 * Invalid/disabled metric indices are rejected without counting as overflow.
 */
int embedids_zephyr_submit(struct embedids_zephyr *service, uint16_t metric_index,
			  embedids_metric_value_t value);

/* Single consumer only. Drains at most one queue capacity and reports every
 * algorithm result after each sample, including OK results. Callback must
 * not block. A non-OK return means ingestion failed, not an anomaly.
 */
embedids_result_t embedids_zephyr_poll(struct embedids_zephyr *service,
				     embedids_zephyr_result_fn callback, void *user);

uint32_t embedids_zephyr_dropped(const struct embedids_zephyr *service);

#ifdef __cplusplus
}
#endif
#endif
