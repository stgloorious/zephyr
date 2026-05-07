/*
 * Copyright (c) 2026 Siemens
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/led/led_trigger.h>
#include <zephyr/drivers/led/led_trigger_timer.h>

LOG_MODULE_DECLARE(led_trigger, CONFIG_LED_LOG_LEVEL);

/** Per-channel data owned by the timer trigger. */
struct led_trigger_timer_data {
	struct k_work_delayable work;
	struct led_trigger_channel *ch;
	uint32_t delay_on;
	uint32_t delay_off;
	uint8_t brightness;
	bool on_phase;
	bool in_use;
};

static struct led_trigger_timer_data
	timer_pool[CONFIG_LED_TRIGGER_MAX_CHANNELS];
static struct k_spinlock timer_pool_lock;

static struct led_trigger_timer_data *timer_data_alloc(void)
{
	k_spinlock_key_t key = k_spin_lock(&timer_pool_lock);

	for (uint32_t i = 0; i < CONFIG_LED_TRIGGER_MAX_CHANNELS; i++) {
		if (!timer_pool[i].in_use) {
			timer_pool[i].in_use = true;
			k_spin_unlock(&timer_pool_lock, key);
			return &timer_pool[i];
		}
	}

	k_spin_unlock(&timer_pool_lock, key);
	return NULL;
}

static void timer_data_free(struct led_trigger_timer_data *td)
{
	k_spinlock_key_t key = k_spin_lock(&timer_pool_lock);

	td->ch = NULL;
	td->in_use = false;

	k_spin_unlock(&timer_pool_lock, key);
}

static void blink_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct led_trigger_timer_data *td =
		CONTAINER_OF(dwork, struct led_trigger_timer_data, work);
	struct led_trigger_channel *ch = td->ch;
	k_spinlock_key_t key;
	uint32_t next_delay;
	uint8_t brightness;
	bool on_phase;

	key = k_spin_lock(&ch->lock);
	if (!ch->active) {
		k_spin_unlock(&ch->lock, key);
		return;
	}

	td->on_phase = !td->on_phase;
	on_phase = td->on_phase;
	brightness = td->brightness;
	next_delay = on_phase ? td->delay_on : td->delay_off;
	k_spin_unlock(&ch->lock, key);

	led_trigger_set_brightness(ch->dev, ch->led_idx,
				   on_phase ? brightness : 0);

	key = k_spin_lock(&ch->lock);
	if (ch->active) {
		k_work_schedule(&td->work, K_MSEC(next_delay));
	}
	k_spin_unlock(&ch->lock, key);
}

static int timer_activate(struct led_trigger_channel *ch)
{
	struct led_trigger_timer_data *td;

	td = timer_data_alloc();
	if (td == NULL) {
		return -ENOMEM;
	}

	td->ch = ch;
	td->on_phase = true;
	td->brightness = LED_BRIGHTNESS_MAX;
	k_work_init_delayable(&td->work, blink_work_handler);

	ch->trigger_data = td;
	return 0;
}

static void timer_deactivate(struct led_trigger_channel *ch)
{
	struct led_trigger_timer_data *td = ch->trigger_data;
	struct k_work_sync sync;

	if (td == NULL) {
		return;
	}

	k_work_cancel_delayable_sync(&td->work, &sync);
	timer_data_free(td);
	ch->trigger_data = NULL;
}

static bool timer_update_brightness(struct led_trigger_channel *ch,
				    uint8_t value)
{
	struct led_trigger_timer_data *td = ch->trigger_data;
	k_spinlock_key_t key;

	if (td == NULL) {
		return false;
	}

	key = k_spin_lock(&ch->lock);
	if (!ch->active) {
		k_spin_unlock(&ch->lock, key);
		return false;
	}
	td->brightness = value;
	k_spin_unlock(&ch->lock, key);
	return true;
}

static const struct led_trigger led_trigger_timer = {
	.name = "timer",
	.activate = timer_activate,
	.deactivate = timer_deactivate,
	.update_brightness = timer_update_brightness,
};

int led_trigger_timer_start(const struct device *dev, uint32_t led,
			    uint32_t delay_on, uint32_t delay_off)
{
	struct led_trigger_channel *ch;
	struct led_trigger_timer_data *td;
	k_spinlock_key_t key;
	uint8_t brightness;

	/* Stop blinking if either delay is zero */
	if (delay_on == 0U || delay_off == 0U) {
		ch = led_trigger_find_channel(dev, led);
		if (ch == NULL) {
			return 0;
		}

		led_trigger_cancel(dev, led);
		return led_trigger_set_brightness(dev, led, 0);
	}

	ch = led_trigger_get_channel(dev, led);
	if (ch == NULL) {
		LOG_WRN("LED trigger pool exhausted "
			"(CONFIG_LED_TRIGGER_MAX_CHANNELS=%u)",
			CONFIG_LED_TRIGGER_MAX_CHANNELS);
		return -ENOMEM;
	}

	key = k_spin_lock(&ch->lock);

	/* Already running as timer trigger - update timing in place */
	if (ch->active && ch->trigger == &led_trigger_timer) {
		td = ch->trigger_data;
		td->delay_on = delay_on;
		td->delay_off = delay_off;

		k_timeout_t next = K_MSEC(td->on_phase ?
					  td->delay_on : td->delay_off);
		k_spin_unlock(&ch->lock, key);
		k_work_reschedule(&td->work, next);
		return 0;
	}

	k_spin_unlock(&ch->lock, key);

	/* Cancel any other active trigger */
	led_trigger_cancel(dev, led);

	/* Activate the timer trigger */
	key = k_spin_lock(&ch->lock);
	ch->trigger = &led_trigger_timer;

	int ret = led_trigger_timer.activate(ch);

	if (ret != 0) {
		ch->trigger = NULL;
		k_spin_unlock(&ch->lock, key);
		return ret;
	}

	td = ch->trigger_data;
	td->delay_on = delay_on;
	td->delay_off = delay_off;
	ch->active = true;
	brightness = td->brightness;

	k_spin_unlock(&ch->lock, key);

	led_trigger_set_brightness(dev, led, brightness);
	k_work_schedule(&td->work, K_MSEC(delay_on));
	return 0;
}
