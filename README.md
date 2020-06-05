![afterglow](https://github.com/smyp/afterglow/blob/master/artwork/afterglow.png "Afterglow")

AFTERGLOW GI is a WPC pinball machine extension board that aims at softening the hard light on/off transitions that result in replacing the original GI incandescent bulbs with LEDs.

The brain of the board is a Arduino nano. It monitors the triac signals on the WPC bus and adjusts the PWM of the GI lines accordingly in order to dim the LEDs. For this purpose the whole GI is powered on DC voltage as opposed to the original AC 6.3V.

![afterglow GI](https://github.com/bitfieldlabs/aggi/raw/master/doc/images/pcb_v10_populated.jpg "Afterglow GI")