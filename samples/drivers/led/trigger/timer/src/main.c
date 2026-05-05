/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief LED blink sample using the LED trigger framework.
 *
 * Demonstrates the led_blink() API on any board with a "led0" alias.
 * When the LED driver does not provide hardware blink (e.g. GPIO LEDs),
 * the LED trigger framework provides a software fallback automatically.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define LED_NODE DT_ALIAS(led0)

static const struct led_dt_spec led = LED_DT_SPEC_GET(LED_NODE);

int main(void)
{
	int err;

	if (!led_is_ready_dt(&led)) {
		LOG_ERR("LED device not ready");
		return -ENODEV;
	}

	/* Start blinking: 500 ms on, 500 ms off (1 Hz) */
	LOG_INF("Blinking LED: 500 ms on, 500 ms off");
	err = led_blink(led.dev, led.index, 500, 500);
	if (err < 0) {
		LOG_ERR("led_blink failed: %d", err);
		return err;
	}
	k_sleep(K_SECONDS(5));

	/* Update blink timing on the fly: 200 ms on, 800 ms off */
	LOG_INF("Updating blink: 200 ms on, 800 ms off");
	err = led_blink(led.dev, led.index, 200, 800);
	if (err < 0) {
		LOG_ERR("led_blink failed: %d", err);
		return err;
	}
	k_sleep(K_SECONDS(5));

	/* led_on() cancels the blink and turns LED on solid */
	LOG_INF("Stopping blink, LED on");
	led_on_dt(&led);
	k_sleep(K_SECONDS(2));

	/* Turn off */
	LOG_INF("LED off");
	led_off_dt(&led);
	k_sleep(K_SECONDS(1));

	LOG_INF("Test complete");

	return 0;
}
