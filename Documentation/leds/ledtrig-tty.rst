===============
LED TTY Trigger
===============

This LED trigger flashes the LED whenever some data flows are happen on the
corresponding TTY device. The TTY device can be freely selected, as well as the
data flow direction.

TTY trigger can be enabled and disabled from user space on led class devices,
that support this trigger as shown below::

	echo tty > trigger
	echo none > trigger

This trigger exports two properties, 'ttyname' and 'dirfilter'. When the
tty trigger is activated both properties are set to default values, which means
no related TTY device yet and the LED would flash on both directions.

Selecting a corresponding trigger TTY::

	echo ttyS0 > ttyname

This LED will now flash on data flow in both directions of 'ttyS0'.

Selecting a direction::

	echo in > dirfilter
	echo out > dirfilter
	echo inout > dirfilter

This selection will flash the LED on data flow in the selected direction.

Example
=======

With the 'dirfilter' property one can use two LEDs to give a user a separate
visual feedback about data flow.

Flash on data send on one LED::

	echo ttyS0 > ttyname
	echo out > dirfilter

Flash on data receive on a second LED::

	echo ttyS0 > ttyname
	echo in > dirfilter
