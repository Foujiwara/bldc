/*
	RFP extras for the VESC firmware.

	Optional functions set on the "RFP Extras" custom configuration page of VESC Tool.
	With the default values the firmware behaves exactly like the official one.

	  - Basic Duty Mode: duty control releases the motor (zero current) instead of braking
	    when the requested duty drops below the present duty.
	  - Hybrid current / duty (forward): the duty is limited proportionally to the request
	    and the current tapers off near the limit.
	  - Braking (Active / Regen Only) and Reverse (Current / None / Duty / Smart) handling of
	    reverse requests, with their limits.
	  - Tractor mode: virtual spring on the wheel position around an anchor that moves with
	    the throttle (current or duty output).

	Hybrid, braking, reverse and tractor act on relative current requests
	(mc_interface_set_current_rel: ADC and PPM apps, VESC Remote, PAS, CAN).

	This file is part of the VESC firmware (GPLv3).
 */

#ifndef RFP_EXT_H_
#define RFP_EXT_H_

#include <stdbool.h>

// Load the saved configuration, register the "RFP Extras" custom config page and start the
// tractor thread.
void rfp_ext_init(void);

// True when Basic Duty Mode is enabled (read from the motor control loop).
bool rfp_ext_basic_duty(void);

// Called by mc_interface_set_current_rel. Returns true when the request was fully handled
// (brake, duty or tractor). Otherwise *val may have been changed and the caller continues
// with the normal current command.
bool rfp_ext_current_rel(float *val);

// One step of the tractor control (called every millisecond by the RFP thread; exported for tests).
void rfp_ext_tractor_update(float dt);

#endif /* RFP_EXT_H_ */
