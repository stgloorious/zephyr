.. zephyr:code-sample:: led-blink
   :name: LED blink
   :relevant-api: led_interface

   Blink a LED using the LED trigger framework.

Overview
********

This sample demonstrates the :c:func:`led_blink` API.  When the underlying
LED driver does not support hardware blink (e.g. GPIO LEDs), the LED trigger
framework provides a software fallback driven by the system workqueue.

The sample:

1. Blinks the LED at 1 Hz (500 ms on, 500 ms off) for 5 seconds.
2. Updates the timing on the fly to 200 ms on / 800 ms off for 5 seconds.
3. Calls :c:func:`led_on` which cancels the blink and turns the LED on.
4. Turns the LED off.

Requirements
************

The board must define an ``led0`` alias pointing to a LED child node
(typically under a ``gpio-leds`` or ``pwm-leds`` parent).  Most Zephyr
boards already provide this.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/led/trigger/timer
   :board: nucleo_h563zi
   :goals: build flash

After flashing, the green user LED will blink visibly, and the console shows::

   [00:00:00.000,000] <inf> main: Blinking LED: 500 ms on, 500 ms off
   [00:00:05.000,000] <inf> main: Updating blink: 200 ms on, 800 ms off
   [00:00:10.000,000] <inf> main: Stopping blink, LED on
   [00:00:12.000,000] <inf> main: LED off
   [00:00:13.000,000] <inf> main: Test complete
