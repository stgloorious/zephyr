/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Siemens
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LED trigger framework — timer trigger implementation.
 *
 * LED triggers are kernel-based sources of LED events, modelled after
 * the Linux LED trigger subsystem.  A trigger drives one or more LED
 * channels according to a pattern or system event.  The LED driver
 * itself only implements brightness control; all pattern logic lives
 * in the trigger layer.  Examples of triggers:
 *
 *   - timer:     periodic on/off blink (implemented here)
 *   - heartbeat: double-pulse heartbeat pattern
 *   - activity:  brief flash on disk, network, or CPU activity
 *   - panic:     fast blink on kernel panic
 *
 * This file implements the "timer" trigger as the first (and currently
 * only) trigger.  It acts as the fallback for led_blink() when the
 * driver does not provide hardware blink support.
 *
 * Adding a new trigger involves:
 *   1. Implementing activate/deactivate logic in a new source file.
 *   2. Providing a runtime API to bind a LED channel to the trigger,
 *      e.g. led_trigger_set(dev, led, &heartbeat_trigger).
 *   3. Optionally exposing trigger selection through shell or DT.
 *
 * Per-channel state is allocated at compile time from devicetree for
 * all standard LED-compatible nodes (gpio-leds, pwm-leds, dac-leds).
 * The blink is driven by a delayable work item on the system workqueue,
 * so timing accuracy depends on system load.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#include "led_trigger.h"

LOG_MODULE_REGISTER(led_trigger, CONFIG_LED_LOG_LEVEL);

struct led_trigger_channel {
	struct k_work_delayable work;
	const struct device *dev;
	uint32_t led;
	uint32_t delay_on;
	uint32_t delay_off;
	bool active;
	bool state; /* true = ON phase, false = OFF phase */
};

struct led_trigger_device {
	const struct device *dev;
	struct led_trigger_channel *channels;
	uint16_t num_channels;
};

#define LED_TRIGGER_CHANNEL_INIT(child_node)				\
	{								\
		.dev = DEVICE_DT_GET(DT_PARENT(child_node)),		\
		.led = DT_NODE_CHILD_IDX(child_node),			\
	},

#define LED_TRIGGER_CHANNELS_DEFINE(parent_node)			\
	static struct led_trigger_channel				\
		_CONCAT(led_trig_ch_, DT_DEP_ORD(parent_node))[] = {	\
		DT_FOREACH_CHILD_STATUS_OKAY(				\
			parent_node, LED_TRIGGER_CHANNEL_INIT)		\
	};

#define LED_TRIGGER_DEVICE_ENTRY(parent_node)				\
	{								\
		.dev = DEVICE_DT_GET(parent_node),			\
		.channels = _CONCAT(led_trig_ch_,			\
				    DT_DEP_ORD(parent_node)),		\
		.num_channels = ARRAY_SIZE(				\
			_CONCAT(led_trig_ch_,				\
				DT_DEP_ORD(parent_node))),		\
	},

/*
 * Generate per-channel arrays for every LED-compatible node.
 * Add new compatibles here when new LED driver types are introduced.
 */
#if DT_HAS_COMPAT_STATUS_OKAY(gpio_leds)
DT_FOREACH_STATUS_OKAY(gpio_leds, LED_TRIGGER_CHANNELS_DEFINE)
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(pwm_leds)
DT_FOREACH_STATUS_OKAY(pwm_leds, LED_TRIGGER_CHANNELS_DEFINE)
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(dac_leds)
DT_FOREACH_STATUS_OKAY(dac_leds, LED_TRIGGER_CHANNELS_DEFINE)
#endif

/* Collect all devices in a plain static array. */
static struct led_trigger_device trigger_devices[] = {
#if DT_HAS_COMPAT_STATUS_OKAY(gpio_leds)
	DT_FOREACH_STATUS_OKAY(gpio_leds, LED_TRIGGER_DEVICE_ENTRY)
#endif
#if DT_HAS_COMPAT_STATUS_OKAY(pwm_leds)
	DT_FOREACH_STATUS_OKAY(pwm_leds, LED_TRIGGER_DEVICE_ENTRY)
#endif
#if DT_HAS_COMPAT_STATUS_OKAY(dac_leds)
	DT_FOREACH_STATUS_OKAY(dac_leds, LED_TRIGGER_DEVICE_ENTRY)
#endif
};

#define NUM_TRIGGER_DEVICES ARRAY_SIZE(trigger_devices)

static struct led_trigger_channel *led_trigger_find_channel(
	const struct device *dev, uint32_t led)
{
	for (size_t i = 0; i < NUM_TRIGGER_DEVICES; i++) {
		if (trigger_devices[i].dev == dev &&
		    led < trigger_devices[i].num_channels) {
			return &trigger_devices[i].channels[led];
		}
	}

	return NULL;
}

/**
 * Set LED brightness directly through the driver API, bypassing
 * z_impl_led_set_brightness() to avoid cancelling the active trigger.
 */
static int led_trigger_set_brightness(const struct device *dev,
				      uint32_t led, uint8_t value)
{
	const struct led_driver_api *api = DEVICE_API_GET(led, dev);

	if (api->set_brightness != NULL) {
		return api->set_brightness(dev, led, value);
	}

	if (value > 0 && api->on != NULL) {
		return api->on(dev, led);
	}

	if (value == 0U && api->off != NULL) {
		return api->off(dev, led);
	}

	return -ENOSYS;
}

static void led_trigger_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct led_trigger_channel *ch =
		CONTAINER_OF(dwork, struct led_trigger_channel, work);
	uint32_t next_delay;

	if (!ch->active) {
		return;
	}

	ch->state = !ch->state;

	if (ch->state) {
		led_trigger_set_brightness(ch->dev, ch->led,
					   LED_BRIGHTNESS_MAX);
		next_delay = ch->delay_on;
	} else {
		led_trigger_set_brightness(ch->dev, ch->led, 0);
		next_delay = ch->delay_off;
	}

	k_work_schedule(&ch->work, K_MSEC(next_delay));
}

static int led_trigger_init(void)
{
	for (size_t i = 0; i < NUM_TRIGGER_DEVICES; i++) {
		struct led_trigger_device *tdev = &trigger_devices[i];

		for (uint16_t j = 0; j < tdev->num_channels; j++) {
			k_work_init_delayable(&tdev->channels[j].work,
					      led_trigger_work_handler);
		}
	}

	return 0;
}

SYS_INIT(led_trigger_init, POST_KERNEL, CONFIG_LED_INIT_PRIORITY);

int led_trigger_blink(const struct device *dev, uint32_t led,
		      uint32_t delay_on, uint32_t delay_off)
{
	struct led_trigger_channel *ch;

	ch = led_trigger_find_channel(dev, led);
	if (ch == NULL) {
		return -ENODEV;
	}

	/* delay_on=0 && delay_off=0 means stop blinking */
	if (delay_on == 0U && delay_off == 0U) {
		ch->active = false;
		k_work_cancel_delayable(&ch->work);
		return led_trigger_set_brightness(dev, led, 0);
	}

	ch->delay_on = delay_on;
	ch->delay_off = delay_off;

	if (ch->active) {
		/* Already blinking — timing updated, next cycle uses
		 * the new values.
		 */
		return 0;
	}

	ch->active = true;
	ch->state = true;

	/* Begin in the ON phase */
	led_trigger_set_brightness(dev, led, LED_BRIGHTNESS_MAX);
	k_work_schedule(&ch->work, K_MSEC(delay_on));

	return 0;
}

void led_trigger_cancel(const struct device *dev, uint32_t led)
{
	struct led_trigger_channel *ch;

	ch = led_trigger_find_channel(dev, led);
	if (ch == NULL || !ch->active) {
		return;
	}

	ch->active = false;
	k_work_cancel_delayable(&ch->work);
}
