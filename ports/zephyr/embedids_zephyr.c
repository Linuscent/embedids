/* SPDX-License-Identifier: Apache-2.0 */
#include <embedids/zephyr.h>
#include <errno.h>
#include <string.h>

static embedids_result_t validate(const embedids_system_config_t *config)
{
	embedids_result_t result = embedids_validate_config(config);

	if (result != EMBEDIDS_OK) {
		return result;
	}
	if (config->num_active_metrics > config->max_metrics) {
		return EMBEDIDS_ERROR_CONFIG_INVALID;
	}
	for (uint32_t i = 0; i < config->num_active_metrics; ++i) {
		const embedids_metric_config_t *entry = &config->metrics[i];
		const embedids_metric_t *metric = &entry->metric;

		if (!memchr(metric->name, '\0', sizeof(metric->name)) || !metric->name[0] ||
		    !metric->history || !metric->max_history_size ||
		    metric->current_size > metric->max_history_size ||
		    metric->write_index >= metric->max_history_size ||
		    metric->type < EMBEDIDS_METRIC_TYPE_UINT32 ||
		    metric->type > EMBEDIDS_METRIC_TYPE_ENUM ||
		    entry->num_algorithms > EMBEDIDS_MAX_ALGORITHMS_PER_METRIC) {
			return EMBEDIDS_ERROR_CONFIG_INVALID;
		}
		for (uint32_t j = 0; j < i; ++j) {
			if (!strcmp(metric->name, config->metrics[j].metric.name)) {
				return EMBEDIDS_ERROR_CONFIG_INVALID;
			}
		}
		for (uint32_t j = 0; j < entry->num_algorithms; ++j) {
			const embedids_algorithm_t *algorithm = &entry->algorithms[j];

			if (algorithm->type == EMBEDIDS_ALGORITHM_TREND ||
			    (algorithm->type == EMBEDIDS_ALGORITHM_THRESHOLD &&
			     metric->type == EMBEDIDS_METRIC_TYPE_BOOL)) {
				return EMBEDIDS_ERROR_ALGORITHM_NOT_SUPPORTED;
			}
			if (algorithm->type != EMBEDIDS_ALGORITHM_THRESHOLD &&
			    algorithm->type != EMBEDIDS_ALGORITHM_CUSTOM) {
				return EMBEDIDS_ERROR_CONFIG_INVALID;
			}
			if (algorithm->type == EMBEDIDS_ALGORITHM_CUSTOM &&
			    !algorithm->config.custom.function) {
				return EMBEDIDS_ERROR_CUSTOM_ALGORITHM_NULL;
			}
		}
	}
	return EMBEDIDS_OK;
}

embedids_result_t embedids_zephyr_init(struct embedids_zephyr *service,
				     embedids_system_config_t *config)
{
	if (!service) {
		return EMBEDIDS_ERROR_INVALID_PARAM;
	}
	embedids_result_t result = validate(config);

	if (result != EMBEDIDS_OK) {
		return result;
	}
	k_msgq_init(&service->samples, service->queue_buffer,
		    sizeof(struct embedids_zephyr_sample), CONFIG_EMBEDIDS_SAMPLE_QUEUE_SIZE);
	atomic_clear(&service->dropped);
	return embedids_init(&service->core, config);
}

int embedids_zephyr_submit(struct embedids_zephyr *service, uint16_t metric_index,
			  embedids_metric_value_t value)
{
	if (!service || !service->core.initialized) {
		return -EINVAL;
	}
	const embedids_system_config_t *config = service->core.system_config;

	if (metric_index >= config->num_active_metrics ||
	    !config->metrics[metric_index].metric.enabled) {
		return -EINVAL;
	}
	struct embedids_zephyr_sample sample = {
		.timestamp_ms = (uint64_t)k_uptime_get(),
		.value = value,
		.metric_index = metric_index,
	};
	int result = k_msgq_put(&service->samples, &sample, K_NO_WAIT);

	if (result) {
		atomic_inc(&service->dropped);
	}
	return result;
}

embedids_result_t embedids_zephyr_poll(struct embedids_zephyr *service,
				     embedids_zephyr_result_fn callback, void *user)
{
	if (!service || !service->core.initialized) {
		return EMBEDIDS_ERROR_NOT_INITIALIZED;
	}
	struct embedids_zephyr_sample sample;
	embedids_system_config_t *config = service->core.system_config;

	for (uint32_t i = 0; i < CONFIG_EMBEDIDS_SAMPLE_QUEUE_SIZE; ++i) {
		if (k_msgq_get(&service->samples, &sample, K_NO_WAIT)) {
			break;
		}
		embedids_result_t result = embedids_add_datapoint(
			&service->core, config->metrics[sample.metric_index].metric.name,
			sample.value, sample.timestamp_ms);

		if (result != EMBEDIDS_OK) {
			return result;
		}
		embedids_metric_config_t *entry = &config->metrics[sample.metric_index];

		if (!entry->metric.enabled || !entry->metric.current_size ||
		    !entry->num_algorithms) {
			continue;
		}
		embedids_result_t results[EMBEDIDS_MAX_ALGORITHMS_PER_METRIC];
		uint32_t count;

		(void)embedids_analyze_metric_detailed(&service->core, entry->metric.name,
						     results, ARRAY_SIZE(results), &count);
		for (uint32_t j = 0; callback && j < count; ++j) {
			callback(sample.metric_index, j, results[j], user);
		}
	}
	return EMBEDIDS_OK;
}

uint32_t embedids_zephyr_dropped(const struct embedids_zephyr *service)
{
	return service ? (uint32_t)atomic_get(&service->dropped) : 0;
}
