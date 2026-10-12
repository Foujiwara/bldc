/*
	RFP extras for the VESC firmware.

	Optional functions set on the "RFP Extras" custom configuration page of VESC Tool.
	With the default values the firmware behaves exactly like the official one.

	  - Basic Duty Mode: duty control releases the motor (zero current) instead of braking
	    when the requested duty drops below the present duty.
	  - Hybrid current / duty (forward): the duty is limited proportionally to the request
	    and the current tapers off near the limit.
	  - Braking (Active / Regen Only) and Reverse (Current / None / Duty) handling of
	    reverse requests, with their limits, ramp and the re-press to reverse sequence.
	  - Tractor mode: virtual spring on the wheel position around an anchor that moves with
	    the throttle (current or duty output).
	  - ADC control type: replaces the Control Type of the ADC app (signed current from ADC1
	    forward and ADC2 reverse, brake priority).

	Hybrid, braking, reverse and tractor act on relative current requests
	(mc_interface_set_current_rel: ADC and PPM apps, VESC Remote, PAS, CAN).

	This file is part of the VESC firmware (GPLv3).
 */

#ifndef RFP_EXT_H_
#define RFP_EXT_H_

#include <stdbool.h>

// Load the saved configuration, register the "RFP Extras" custom config page and start the
// RFP thread (continuous computation, 1 kHz).
void rfp_ext_init(void);

// True when Basic Duty Mode is enabled (read from the motor control loop).
bool rfp_ext_basic_duty(void);

// Called by mc_interface_set_current_rel. When hybrid, braking / reverse or tractor is enabled the
// request is stored and true is returned: the RFP thread then computes the motor command from the
// last request every millisecond. Otherwise false is returned and nothing changes.
bool rfp_ext_current_rel(float *val);

// Called by mc_interface_set_duty. With Basic Duty Mode the request is stored and true is returned:
// the RFP thread releases the motor (zero current) while the request is below the present duty and
// follows it in duty otherwise. Otherwise (or from the RFP thread) false is returned.
bool rfp_ext_duty(float duty);

// Called by every other mc_interface motor command (current, brake, duty, release...): stops the
// continuous computation, so that command takes over (commands from the RFP thread are ignored).
void rfp_ext_cancel(void);

// One step of the continuous computation (called every millisecond by the RFP thread; exported
// for tests).
void rfp_ext_update(float dt);

// ADC control types (read by the ADC app): 0 = Control Type of the ADC app, 1 = Current Reverse
// ADC2: signed relative current, ADC1 forward, ADC2 reverse.
#define RFP_ADC_CTRL_APP				0
#define RFP_ADC_CTRL_CURRENT_REV_ADC2	1
int rfp_ext_adc_ctrl(void);

// Signed request (-1 to 1) from the decoded ADC1 and ADC2 levels (0 to 1) for the RFP ADC control
// types. ADC2 Brake Priority: ADC2 above the deadband cancels ADC1, otherwise ADC1 - ADC2.
float rfp_ext_adc_signed(float adc1, float adc2, float deadband);

#endif /* RFP_EXT_H_ */
