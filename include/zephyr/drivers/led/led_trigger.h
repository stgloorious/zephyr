/*
 * Copyright (c) 2026 Siemens
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_LED_LED_TRIGGER_H_
#define ZEPHYR_INCLUDE_DRIVERS_LED_LED_TRIGGER_H_

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/types.h>

struct led_trigger_channel;

/**
 * @brief LED trigger type definition.
 *
 * Each trigger type (timer, heartbeat, network activity, etc.) provides
 * an instance of this structure with its own activate/deactivate
 * callbacks.  Inspired by Linux's struct led_trigger.
 */
struct led_trigger {
	/** Human-readable trigger name (e.g. "timer", "diskactivity"). */
	const char *name;

	/**
	 * @brief Called when the trigger is attached to a channel.
	 *
	 * The trigger should initialise per-channel state, store a
	 * pointer via @c ch->trigger_data, and prepare to drive the LED.
	 *
	 * @param ch Channel being activated.
	 * @return 0 on success, negative errno on failure.
	 */
	int (*activate)(struct led_trigger_channel *ch);

	/**
	 * @brief Called when the trigger is detached from a channel.
	 *
	 * The trigger must stop all asynchronous work and release any
	 * per-channel resources.
	 *
	 * @param ch Channel being deactivated.
	 */
	void (*deactivate)(struct led_trigger_channel *ch);

	/**
	 * @brief Optional: update brightness while the trigger is active.
	 *
	 * @param ch    Active channel.
	 * @param value New brightness (0..LED_BRIGHTNESS_MAX).
	 * @return true if the update was handled, false otherwise.
	 */
	bool (*update_brightness)(struct led_trigger_channel *ch,
				  uint8_t value);
};

/**
 * @brief Per-channel trigger state managed by the core.
 */
struct led_trigger_channel {
	const struct device *dev;
	uint32_t led_idx;
	const struct led_trigger *trigger;
	void *trigger_data;
	struct k_spinlock lock;
	bool active;
};

/**
 * @brief Find an existing trigger channel for a (device, led) pair.
 *
 * @param dev LED device.
 * @param led LED index on the device.
 * @return Pointer to the channel, or NULL if none is allocated.
 */
struct led_trigger_channel *led_trigger_find_channel(const struct device *dev,
						     uint32_t led);

/**
 * @brief Get or allocate a trigger channel for a (device, led) pair.
 *
 * @param dev LED device.
 * @param led LED index on the device.
 * @return Pointer to the channel, or NULL if the pool is exhausted.
 */
struct led_trigger_channel *led_trigger_get_channel(const struct device *dev,
						    uint32_t led);

/**
 * @brief Cancel any active trigger on a (device, led) channel.
 *
 * Calls the trigger's deactivate callback and clears the binding.
 *
 * @param dev LED device.
 * @param led LED index on the device.
 */
void led_trigger_cancel(const struct device *dev, uint32_t led);

/**
 * @brief Notify the active trigger of a brightness change.
 *
 * Delegates to the trigger's update_brightness callback if available.
 *
 * @param dev   LED device.
 * @param led   LED index on the device.
 * @param value New brightness (0...LED_BRIGHTNESS_MAX).
 * @return true if a trigger handled the update, false otherwise.
 */
bool led_trigger_update_brightness(const struct device *dev, uint32_t led,
				   uint8_t value);

/**
 * @brief Set LED brightness via the driver (helper for triggers).
 *
 * Calls the driver's set_brightness callback, falling back to on/off
 * if the driver does not support brightness control.
 *
 * @param dev   LED device.
 * @param led   LED index on the device.
 * @param value Brightness (0..LED_BRIGHTNESS_MAX).
 * @return 0 on success, negative errno on failure.
 */
int led_trigger_set_brightness(const struct device *dev, uint32_t led,
			       uint8_t value);

#endif /* ZEPHYR_INCLUDE_DRIVERS_LED_LED_TRIGGER_H_ */
