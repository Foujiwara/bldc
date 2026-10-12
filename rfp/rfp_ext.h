/*
	RFP extras for the VESC firmware.

	Three optional functions, each with its own switch in the "RFP Extras" custom
	configuration page of VESC Tool. All are OFF by default: with every switch off the
	firmware behaves exactly like the official one.

	  - Basic Duty Mode: duty control releases the motor (zero current) instead of braking
	    when the requested duty drops below the present duty.
	  - Hybrid current / duty: for relative current requests (apps, CAN), the duty is
	    limited proportionally to the request and the current tapers off near the limit.
	  - Smart Reverse: a negative relative current request brakes while rolling forward,
	    then reverses at a limited duty reached with a ramp.

	This file is part of the VESC firmware (GPLv3).
 */

#ifndef RFP_EXT_H_
#define RFP_EXT_H_

#include <stdbool.h>

// Load the saved configuration and register the "RFP Extras" custom config page.
void rfp_ext_init(void);

// True when Basic Duty Mode is enabled (read from the motor control loop).
bool rfp_ext_basic_duty(void);

// Called by mc_interface_set_current_rel. Returns true when the request was fully handled
// (brake or reverse duty was commanded). Otherwise *val may have been reduced (hybrid mode)
// and the caller continues with the normal current command.
bool rfp_ext_current_rel(float *val);

#endif /* RFP_EXT_H_ */
