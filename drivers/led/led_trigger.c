/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Siemens
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LED trigger framework - timer trigger implementation.
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
 * LED drivers opt in to trigger support by placing LED_TRIGGER_REGISTER()
 * inside their instantiation macro.  The linker collects all registrations
 * into an iterable section that this file iterates at runtime.
 *
 * Concurrency:
 *   A per-channel spinlock protects the active flag, timing parameters,
 *   and phase state against races between the work handler (system
 *   workqueue) and API callers (arbitrary threads/ISRs).
 *
 *   led_trigger_cancel() uses k_work_cancel_delayable() which is
 *   best-effort: if the handler is already executing past the lock
 *   acquisition, it will complete one last brightness write but will
 *   not reschedule (it checks ch->active under the lock before
 *   scheduling the next iteration).
 */

#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/logging/log.h>

#include "led_trigger.h"

LOG_MODULE_REGISTER(led_trigger, CONFIG_LED_LOG_LEVEL);

static struct led_trigger_channel *led_trigger_find_channel(
	const struct device *dev, uint32_t led)
{
	STRUCT_SECTION_FOREACH(led_trigger_device, tdev) {
		if (tdev->dev == dev && led < tdev->num_channels) {
			return &tdev->channels[led];
		}
	}

	return NULL;
}

/**
 * Set LED brightness directly through the driver API, bypassing
 * z_impl_led_set_brightness() to avoid recursion into the trigger layer.
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
	k_spinlock_key_t key;
	uint32_t next_delay;
	uint8_t brightness;
	bool on_phase;
	int ret;

	key = k_spin_lock(&ch->lock);

	if (!ch->active) {
		k_spin_unlock(&ch->lock, key);
		return;
	}

	ch->on_phase = !ch->on_phase;
	on_phase = ch->on_phase;
	brightness = ch->brightness;
	next_delay = on_phase ? ch->delay_on : ch->delay_off;

	k_spin_unlock(&ch->lock, key);

	if (on_phase) {
		ret = led_trigger_set_brightness(ch->dev, ch->led_idx,
						 brightness);
	} else {
		ret = led_trigger_set_brightness(ch->dev, ch->led_idx, 0);
	}

	if (ret < 0) {
		LOG_WRN("LED %s channel %u brightness write failed: %d",
			ch->dev->name, ch->led_idx, ret);
	}

	key = k_spin_lock(&ch->lock);

	if (ch->active) {
		k_work_schedule(&ch->work, K_MSEC(next_delay));
	}

	k_spin_unlock(&ch->lock, key);
}

static int led_trigger_init(void)
{
	STRUCT_SECTION_FOREACH(led_trigger_device, tdev) {
		for (uint32_t j = 0; j < tdev->num_channels; j++) {
			k_work_init_delayable(&tdev->channels[j].work,
					      led_trigger_work_handler);
		}
	}

	return 0;
}

/*
 * Init priority: same as LED drivers.  This is safe because
 * led_trigger_init() only performs memory initialization
 * (k_work_init_delayable) and does not touch hardware.
 */
SYS_INIT(led_trigger_init, POST_KERNEL, CONFIG_LED_INIT_PRIORITY);

int led_trigger_blink(const struct device *dev, uint32_t led,
		      uint32_t delay_on, uint32_t delay_off)
{
	struct led_trigger_channel *ch;
	k_spinlock_key_t key;

	ch = led_trigger_find_channel(dev, led);
	if (ch == NULL) {
		return -ENODEV;
	}

	/* Both zero: stop blinking */
	if (delay_on == 0U && delay_off == 0U) {
		key = k_spin_lock(&ch->lock);
		ch->active = false;
		k_spin_unlock(&ch->lock, key);

		k_work_cancel_delayable(&ch->work);
		return led_trigger_set_brightness(dev, led, 0);
	}

	/* Reject one-sided zero delays - they would produce a
	 * near-instantaneous flash with K_MSEC(0) scheduling.
	 */
	if (delay_on == 0U || delay_off == 0U) {
		return -EINVAL;
	}

	key = k_spin_lock(&ch->lock);

	ch->delay_on = delay_on;
	ch->delay_off = delay_off;

	if (ch->active) {
		k_spin_unlock(&ch->lock, key);

		/* Already blinking - cancel and reschedule so the new
		 * timing takes effect immediately.
		 */
		k_work_cancel_delayable(&ch->work);

		key = k_spin_lock(&ch->lock);
		if (ch->active) {
			k_work_schedule(&ch->work,
					K_MSEC(ch->on_phase ?
						ch->delay_on :
						ch->delay_off));
		}
		k_spin_unlock(&ch->lock, key);

		return 0;
	}

	ch->active = true;
	ch->on_phase = true;
	ch->brightness = LED_BRIGHTNESS_MAX;

	k_spin_unlock(&ch->lock, key);

	/* Begin in the ON phase */
	led_trigger_set_brightness(dev, led, ch->brightness);
	k_work_schedule(&ch->work, K_MSEC(delay_on));

	return 0;
}

void led_trigger_cancel(const struct device *dev, uint32_t led)
{
	struct led_trigger_channel *ch;
	k_spinlock_key_t key;

	ch = led_trigger_find_channel(dev, led);
	if (ch == NULL) {
		return;
	}

	key = k_spin_lock(&ch->lock);

	if (!ch->active) {
		k_spin_unlock(&ch->lock, key);
		return;
	}

	ch->active = false;
	k_spin_unlock(&ch->lock, key);

	/* Best-effort cancel: if the handler is already past its
	 * active check it will complete one brightness write but
	 * will not reschedule (it re-checks active under the lock
	 * before calling k_work_schedule).
	 */
	k_work_cancel_delayable(&ch->work);
}

void led_trigger_update_brightness(const struct device *dev, uint32_t led,
				   uint8_t value)
{
	struct led_trigger_channel *ch;
	k_spinlock_key_t key;

	ch = led_trigger_find_channel(dev, led);
	if (ch == NULL) {
		return;
	}

	key = k_spin_lock(&ch->lock);

	if (ch->active) {
		ch->brightness = value;
	}

	k_spin_unlock(&ch->lock, key);
}
