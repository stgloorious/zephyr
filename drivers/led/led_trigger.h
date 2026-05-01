/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Siemens
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Internal API declarations for the LED trigger framework.
 *
 * LED triggers are kernel-based sources of LED events.  A trigger drives
 * one or more LEDs according to a specific pattern or system event.
 * Examples include:
 *   - timer:    periodic on/off blink
 *   - heartbeat: double-pulse heartbeat pattern
 *   - activity: brief flash on disk, network, or CPU activity
 *   - panic:    fast blink on kernel panic
 *
 * The framework is designed so that additional triggers can be added
 * without modifying LED drivers or the core dispatch layer.
 *
 * LED drivers opt in to trigger support by placing LED_TRIGGER_REGISTER()
 * inside their instantiation macro.  The linker collects all registrations
 * into an iterable section that the trigger framework iterates at runtime.
 */

#ifndef ZEPHYR_DRIVERS_LED_LED_TRIGGER_H_
#define ZEPHYR_DRIVERS_LED_LED_TRIGGER_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/types.h>

/**
 * @brief Per-channel state for the timer trigger.
 */
struct led_trigger_channel {
	struct k_work_delayable work;
	struct k_spinlock lock;
	const struct device *dev;
	uint32_t led_idx;
	uint32_t delay_on;
	uint32_t delay_off;
	uint8_t brightness;
	bool active;
	bool on_phase;
};

/**
 * @brief Per-device registration entry collected via iterable section.
 */
struct led_trigger_device {
	const struct device *dev;
	struct led_trigger_channel *channels;
	uint32_t num_channels;
};

/**
 * @brief Register a LED device node for trigger support.
 *
 * Place this macro inside the driver's per-instance instantiation macro.
 * It allocates per-channel trigger state and registers the device with
 * the trigger framework via an iterable section entry.
 *
 * @param node_id Devicetree node identifier of the LED controller.
 */
#define LED_TRIGGER_CHANNEL_INIT(child_node)				\
	{								\
		.dev = DEVICE_DT_GET(DT_PARENT(child_node)),		\
		.led_idx = DT_NODE_CHILD_IDX(child_node),		\
	},

#define LED_TRIGGER_REGISTER(node_id)					\
	IF_ENABLED(CONFIG_LED_TRIGGER, (				\
		static struct led_trigger_channel			\
			_CONCAT(led_trig_ch_, DT_DEP_ORD(node_id))[] = {\
			DT_FOREACH_CHILD(				\
				node_id, LED_TRIGGER_CHANNEL_INIT)	\
		};							\
		static STRUCT_SECTION_ITERABLE(led_trigger_device,	\
			_CONCAT(led_trig_dev_, DT_DEP_ORD(node_id))) = {\
			.dev = DEVICE_DT_GET(node_id),			\
			.channels = _CONCAT(led_trig_ch_,		\
					    DT_DEP_ORD(node_id)),	\
			.num_channels = DT_CHILD_NUM(node_id),		\
		};							\
	))

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
 * Best-effort cancellation: if the work handler is already executing,
 * it will complete its current iteration but will not reschedule.
 * One final brightness write may still occur after this returns.
 *
 * @param dev LED device.
 * @param led LED channel number.
 */
void led_trigger_cancel(const struct device *dev, uint32_t led);

/**
 * @brief Update the ON-phase brightness of an active blink.
 *
 * If a software blink is active on the channel, the next ON phase will
 * use @p value as its brightness.  If the channel is not blinking, this
 * function has no effect.
 *
 * @param dev LED device.
 * @param led LED channel number.
 * @param value Brightness value (0-100) for the ON phase.
 */
void led_trigger_update_brightness(const struct device *dev, uint32_t led,
				   uint8_t value);

#endif /* ZEPHYR_DRIVERS_LED_LED_TRIGGER_H_ */
