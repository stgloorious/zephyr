/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Siemens AG
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_bitbang_pwm

#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

LOG_MODULE_REGISTER(pwm_bitbang, CONFIG_PWM_LOG_LEVEL);

struct pwm_bitbang_config {
	const struct device *timer;
	const struct gpio_dt_spec *gpios; /* array of GPIO PWM outputs */
	uint8_t num_channels;
};

struct pwm_bitbang_data {
	struct k_spinlock lock;
	uint32_t *pulse; /* array of channel pulse widths */
	uint32_t freq;   /* frequency determined by the counter configuration */
	uint32_t period; /* period is the same for all channels */
	uint32_t tick;   /* current tick in the period */
	bool running;    /* true after first set_cycles configures the PWM */
};

static void pwm_bitbang_timer_cb(const struct device *counter_dev,
				 void *user_data)
{
	const struct device *dev = user_data;
	const struct pwm_bitbang_config *cfg = dev->config;
	struct pwm_bitbang_data *data = dev->data;

	ARG_UNUSED(counter_dev);

	if (!data->running) {
		return;
	}

	/* Iterate over GPIOs and set the level based on desired pulse width */
	for (uint8_t i = 0; i < cfg->num_channels; i++) {
		gpio_pin_set_dt(&cfg->gpios[i],
				data->tick < data->pulse[i] ? 1 : 0);
	}

	data->tick++;
	if (data->tick >= data->period) {
		data->tick = 0;
	}
}

static int pwm_bitbang_set_cycles(const struct device *dev, uint32_t channel,
				  uint32_t period_cycles, uint32_t pulse_cycles,
				  pwm_flags_t flags)
{
	const struct pwm_bitbang_config *cfg = dev->config;
	struct pwm_bitbang_data *data = dev->data;
	k_spinlock_key_t key;

	if (channel >= cfg->num_channels) {
		return -EINVAL;
	}

	if (flags & ~PWM_POLARITY_MASK) {
		LOG_ERR("unsupported flags 0x%x", flags);
		return -ENOTSUP;
	}

	if (period_cycles == 0) {
		return -EINVAL;
	}

	LOG_DBG("set_cycles: ch=%u period=%u pulse=%u flags=0x%x",
		channel, period_cycles, pulse_cycles, flags);

	if (flags & PWM_POLARITY_INVERTED) {
		pulse_cycles = period_cycles - pulse_cycles;
	}

	key = k_spin_lock(&data->lock);
	data->period = period_cycles;
	data->pulse[channel] = pulse_cycles;
	data->tick = 0;
	data->running = true;
	k_spin_unlock(&data->lock, key);

	return 0;
}

static int pwm_bitbang_get_cycles_per_sec(const struct device *dev,
					  uint32_t channel, uint64_t *cycles)
{
	const struct pwm_bitbang_data *data = dev->data;

	*cycles = data->freq;
	return 0;
}

static int pwm_bitbang_init(const struct device *dev)
{
	const struct pwm_bitbang_config *cfg = dev->config;
	struct pwm_bitbang_data *data = dev->data;
	const struct device *timer = cfg->timer;
	int ret;

	LOG_DBG("init: dev=%s channels=%u", dev->name, cfg->num_channels);

	if (!device_is_ready(timer)) {
		LOG_ERR("Counter device %s not ready", timer->name);
		return -ENODEV;
	}

	for (uint8_t i = 0; i < cfg->num_channels; i++) {
		if (!gpio_is_ready_dt(&cfg->gpios[i])) {
			LOG_ERR("GPIO for channel %u not ready", i);
			return -ENODEV;
		}
		ret = gpio_pin_configure_dt(&cfg->gpios[i], GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("GPIO config ch %u failed: %d", i, ret);
			return ret;
		}
	}

	data->freq = counter_get_frequency(timer);
	if (data->freq == 0) {
		LOG_ERR("Counter frequency is 0");
		return -ENODEV;
	}

	data->period = 1;
	data->tick = 0;
	data->running = false;

	LOG_DBG("init: counter freq=%u Hz (= cycles/sec)", data->freq);

	/* Fire ISR every counter tick; the counter frequency determines
	 * both the interrupt rate and the PWM resolution.
	 */
	struct counter_top_cfg top_cfg = {
		.ticks = 1,
		.callback = pwm_bitbang_timer_cb,
		.user_data = (void *)dev,
		.flags = 0,
	};

	ret = counter_start(timer);
	if (ret) {
		LOG_ERR("counter_start failed: %d", ret);
		return ret;
	}

	ret = counter_set_top_value(timer, &top_cfg);
	if (ret) {
		LOG_ERR("counter_set_top_value failed: %d", ret);
		return ret;
	}

	LOG_INF("init: running, %u Hz ISR", data->freq);
	return 0;
}

static DEVICE_API(pwm, pwm_bitbang_driver_api) = {
	.set_cycles = pwm_bitbang_set_cycles,
	.get_cycles_per_sec = pwm_bitbang_get_cycles_per_sec,
};

#define MAX_CHANNELS(inst) DT_INST_PROP_LEN(inst, pwm_gpios)

#define PWM_BITBANG_GPIO_SPEC(node_id, prop, idx) \
	GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx),

#define PWM_BITBANG_INIT(inst)                                                     \
	static const struct gpio_dt_spec pwm_bitbang_gpios_##inst[] = {            \
		DT_INST_FOREACH_PROP_ELEM(inst, pwm_gpios,                         \
					  PWM_BITBANG_GPIO_SPEC)                   \
	};                                                                         \
	static uint32_t pwm_bitbang_pulse_##inst[MAX_CHANNELS(inst)];              \
	static struct pwm_bitbang_data pwm_bitbang_data_##inst = {                 \
		.pulse = pwm_bitbang_pulse_##inst,                                 \
	};                                                                         \
	static const struct pwm_bitbang_config pwm_bitbang_config_##inst = {       \
		.timer = DEVICE_DT_GET(DT_INST_PHANDLE(inst, timer)),              \
		.gpios = pwm_bitbang_gpios_##inst,                                 \
		.num_channels = MAX_CHANNELS(inst),                                \
	};                                                                         \
	DEVICE_DT_INST_DEFINE(inst, pwm_bitbang_init, NULL,                        \
			      &pwm_bitbang_data_##inst,                            \
			      &pwm_bitbang_config_##inst,                          \
			      POST_KERNEL,                                         \
			      CONFIG_PWM_INIT_PRIORITY,                            \
			      &pwm_bitbang_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_BITBANG_INIT)
