/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Siemens
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Internal API for the LED trigger framework.
 *
 * LED triggers are kernel-based sources of LED events.  A trigger drives
 * one or more LEDs according to a specific pattern or system event.
 * Examples include:
 *   - timer:    periodic on/off blink (this file implements the timer trigger)
 *   - heartbeat: double-pulse heartbeat pattern
 *   - activity: brief flash on disk, network, or CPU activity
 *   - panic:    fast blink on kernel panic
 *
 * The framework is designed so that additional triggers can be added
 * without modifying LED drivers or the core dispatch layer.
 */

#ifndef ZEPHYR_DRIVERS_LED_LED_TRIGGER_H_
#define ZEPHYR_DRIVERS_LED_LED_TRIGGER_H_

#include <zephyr/device.h>
#include <zephyr/types.h>

/**
 * @brief Start or update a software blink for a LED channel.
 *
 * If both @p delay_on and @p delay_off are zero, any active blink is
 * stopped and the LED is turned off.
 *
 * @param dev LED device.
 * @param led LED channel number.
 * @param delay_on ON period in milliseconds.
 * @param delay_off OFF period in milliseconds.
 * @return 0 on success, negative on error.
 */
int led_trigger_blink(const struct device *dev, uint32_t led,
		      uint32_t delay_on, uint32_t delay_off);

/**
 * @brief Cancel any active software blink for a LED channel.
 *
 * Does nothing if the channel is not currently blinking.
 *
 * @param dev LED device.
 * @param led LED channel number.
 */
void led_trigger_cancel(const struct device *dev, uint32_t led);

#endif /* ZEPHYR_DRIVERS_LED_LED_TRIGGER_H_ */
