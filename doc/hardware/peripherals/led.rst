.. _led_api:

Light-Emitting Diode (LED)
##########################

Overview
********

The LED API provides access to Light Emitting Diodes, both in individual and
strip form.

.. _led_triggers:

LED Triggers
************

LED triggers are kernel-based sources of LED events, modelled after the
Linux LED trigger subsystem.  A trigger drives one or more LED channels
according to a pattern or system event.  The LED driver itself only needs
to implement brightness control; all pattern logic lives in the trigger
layer.

When :c:func:`led_blink` is called on a LED device whose driver does not
provide a hardware blink callback, the LED trigger framework provides a
software fallback automatically.  No driver modifications are required.

The trigger framework is split into two layers:

- **Trigger core** (``drivers/led/led_trigger.c``, ``led_trigger.h``):
  trigger-agnostic channel pool management, allocation, cancellation, and
  shared helpers.  Defines ``struct led_trigger`` with ``activate``,
  ``deactivate``, and ``update_brightness`` callbacks that each trigger
  type implements.
- **Trigger modules** implement the actual LED driving logic. Each module
  is a self-contained compilation unit with its own Kconfig option:

  - **Timer trigger** (``drivers/led/led_trigger_timer.c``): periodic
    on/off blinking via the system workqueue.
  - Future triggers (network activity, CPU load, etc.) follow
    the same pattern — define ``struct led_trigger`` callbacks and manage
    per-channel state independently.

Timer Trigger
=============

The timer trigger implements periodic on/off blinking driven by the
system workqueue.  It is activated transparently through the
:c:func:`led_blink` API when the driver does not provide hardware blink.
Enabled via :kconfig:option:`CONFIG_LED_TRIGGER_TIMER`.

Behavior:

- :c:func:`led_blink` with either ``delay_on`` or ``delay_off`` set to zero
  stops the blink and turns the LED off.
- :c:func:`led_on` and :c:func:`led_off` cancel any active software blink
  on the channel before applying the requested state.
- :c:func:`led_set_brightness` updates the ON-phase brightness of an active
  blink without cancelling it.  The LED remains in its current blink phase;
  the new brightness takes effect on the next ON cycle.

Adding a New Trigger
====================

To add a new trigger:

1. Define a ``struct led_trigger`` with ``activate``, ``deactivate``, and
   optionally ``update_brightness`` callbacks.
2. Manage per-channel state in a static pool within the trigger module.
3. Use :c:func:`led_trigger_get_channel` to obtain a channel slot and
   :c:func:`led_trigger_set_brightness` to drive the LED from the trigger.
4. Add a Kconfig option (``CONFIG_LED_TRIGGER_HEARTBEAT``) and source file.

Channel State Pool
------------------

The trigger framework manages per-channel state through a fixed-size static
pool.  No heap allocation occurs.  A slot is claimed on the first
:c:func:`led_blink` call for a given (device, channel) pair and remains
assigned for the lifetime of the application.

The pool size is configured with
:kconfig:option:`CONFIG_LED_TRIGGER_MAX_CHANNELS`.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_LED`
* :kconfig:option:`CONFIG_LED_STRIP`
* :kconfig:option:`CONFIG_LED_TRIGGER`
* :kconfig:option:`CONFIG_LED_TRIGGER_TIMER`
* :kconfig:option:`CONFIG_LED_TRIGGER_MAX_CHANNELS`

API Reference
*************

LED
===

.. doxygengroup:: led_interface

LED Strip
=========

.. doxygengroup:: led_strip_interface
