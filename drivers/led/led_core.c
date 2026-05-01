/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Siemens
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LED subsystem core - API dispatch functions.
 *
 * This file contains the z_impl_led_*() dispatch functions for the LED
 * subsystem.  Each function resolves the driver callback, applies
 * fallback logic (e.g. on/off via set_brightness and vice versa), and
 * validates arguments.
 *
 * When CONFIG_LED_TRIGGER is enabled, led_on() and led_off() cancel
 * any active trigger on the channel before dispatching to the driver.
 * led_set_brightness() updates the blink ON-phase brightness without
 * cancelling.  led_blink() routes to the trigger framework when the
 * driver does not provide hardware blink.
 */

#include <zephyr/drivers/led.h>

#ifdef CONFIG_LED_TRIGGER
#include "led_trigger.h"
#endif

int z_impl_led_blink(const struct device *dev, uint32_t led,
		     uint32_t delay_on, uint32_t delay_off)
{
	const struct led_driver_api *api = DEVICE_API_GET(led, dev);

	/* Use hardware blink if the driver provides one */
	if (api->blink != NULL) {
#ifdef CONFIG_LED_TRIGGER
		/* Cancel any active software trigger before switching
		 * to hardware blink.
		 */
		led_trigger_cancel(dev, led);
#endif
		return api->blink(dev, led, delay_on, delay_off);
	}

#ifdef CONFIG_LED_TRIGGER
	return led_trigger_blink(dev, led, delay_on, delay_off);
#else
	return -ENOSYS;
#endif
}

int z_impl_led_get_info(const struct device *dev, uint32_t led,
			const struct led_info **info)
{
	const struct led_driver_api *api = DEVICE_API_GET(led, dev);

	if (api->get_info == NULL) {
		*info = NULL;
		return -ENOSYS;
	}

	return api->get_info(dev, led, info);
}

int z_impl_led_set_brightness(const struct device *dev, uint32_t led,
			      uint8_t value)
{
	const struct led_driver_api *api = DEVICE_API_GET(led, dev);

#ifdef CONFIG_LED_TRIGGER
	/* Update the trigger's ON-phase brightness without cancelling the
	 * blink - consistent with the API contract.
	 */
	led_trigger_update_brightness(dev, led, value);
#endif

	if (api->set_brightness == NULL) {
		if (api->on == NULL || api->off == NULL) {
			return -ENOSYS;
		}
	}

	if (value > LED_BRIGHTNESS_MAX) {
		return -EINVAL;
	}

	if (api->set_brightness == NULL) {
		if (value) {
			return api->on(dev, led);
		} else {
			return api->off(dev, led);
		}
	}

	return api->set_brightness(dev, led, value);
}

int z_impl_led_write_channels(const struct device *dev,
			      uint32_t start_channel,
			      uint32_t num_channels, const uint8_t *buf)
{
	const struct led_driver_api *api = DEVICE_API_GET(led, dev);

	if (api->write_channels == NULL) {
		return -ENOSYS;
	}

	return api->write_channels(dev, start_channel, num_channels, buf);
}

int z_impl_led_set_color(const struct device *dev, uint32_t led,
			 uint8_t num_colors, const uint8_t *color)
{
	const struct led_driver_api *api = DEVICE_API_GET(led, dev);

	if (api->set_color == NULL) {
		return -ENOSYS;
	}

	return api->set_color(dev, led, num_colors, color);
}

int z_impl_led_on(const struct device *dev, uint32_t led)
{
	const struct led_driver_api *api = DEVICE_API_GET(led, dev);

#ifdef CONFIG_LED_TRIGGER
	led_trigger_cancel(dev, led);
#endif

	if (api->set_brightness == NULL && api->on == NULL) {
		return -ENOSYS;
	}

	if (api->on == NULL) {
		return api->set_brightness(dev, led, LED_BRIGHTNESS_MAX);
	}

	return api->on(dev, led);
}

int z_impl_led_off(const struct device *dev, uint32_t led)
{
	const struct led_driver_api *api = DEVICE_API_GET(led, dev);

#ifdef CONFIG_LED_TRIGGER
	led_trigger_cancel(dev, led);
#endif

	if (api->set_brightness == NULL && api->off == NULL) {
		return -ENOSYS;
	}

	if (api->off == NULL) {
		return api->set_brightness(dev, led, 0);
	}

	return api->off(dev, led);
}
