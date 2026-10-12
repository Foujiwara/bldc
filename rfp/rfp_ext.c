/*
	RFP extras for the VESC firmware. See rfp_ext.h.

	This file is part of the VESC firmware (GPLv3).
 */

#include "rfp_ext.h"
#include "rfp_conf_gen.h"
#include "conf_custom.h"
#include "conf_general.h"
#include "mc_interface.h"
#include "utils_math.h"
#include "utils_sys.h"
#include "ch.h"

#include <math.h>
#include <string.h>

// Settings are saved in serialized form (signature included) in emulated EEPROM words at the top
// of the custom range (0 to 127), which LispBM scripts rarely use. Word 0: magic + length.
#define EE_WORDS		(1 + (RFP_CONF_SER_SIZE + 3) / 4)
#define EE_BASE			(128 - EE_WORDS)
#define EE_MAGIC		0x52460000

// Reverse request: relative current below this value
#define REV_REQ			-0.001
// Tractor: release when the throttle requests stop arriving (app stopped or switched to brake)
#define TRACTOR_TIMEOUT	0.1

static rfp_config m_conf;
static volatile bool m_basic_duty = false;

// Reverse state
static float m_smart_cap = 0.0;			// smart reverse duty limit (ramped)
static systime_t m_smart_last = 0;
static bool m_need_neutral = false;		// re-press to reverse
static bool m_was_braking = false;

// Tractor state
static volatile float m_tr_req = 0.0;
static volatile systime_t m_tr_req_time = 0;
static volatile bool m_tr_req_valid = false;
static bool m_tr_active = false;
static float m_tr_anchor = 0.0;			// electrical degrees

static void sanitize(rfp_config *c) {
	float *f[] = { &c->hybrid_duty_limit, &c->hybrid_band, &c->stop_erpm, &c->brake_taper_start_erpm,
			&c->brake_taper_min, &c->rev_cur_max, &c->rev_cur_duty_max, &c->rev_band, &c->rev_duty_max,
			&c->smart_cur_max, &c->smart_duty_max, &c->smart_ramp, &c->tractor_max_erpm, &c->tractor_freedom,
			&c->tractor_stiffness, &c->tractor_brake, &c->tractor_max_current, &c->tractor_max_lag,
			&c->tractor_duty_stiffness, &c->tractor_duty_brake, &c->tractor_max_duty };
	rfp_config d;
	rfp_conf_set_defaults(&d);
	float *fd[] = { &d.hybrid_duty_limit, &d.hybrid_band, &d.stop_erpm, &d.brake_taper_start_erpm,
			&d.brake_taper_min, &d.rev_cur_max, &d.rev_cur_duty_max, &d.rev_band, &d.rev_duty_max,
			&d.smart_cur_max, &d.smart_duty_max, &d.smart_ramp, &d.tractor_max_erpm, &d.tractor_freedom,
			&d.tractor_stiffness, &d.tractor_brake, &d.tractor_max_current, &d.tractor_max_lag,
			&d.tractor_duty_stiffness, &d.tractor_duty_brake, &d.tractor_max_duty };
	for (unsigned i = 0;i < sizeof(f) / sizeof(f[0]);i++) {
		if (!isfinite(*f[i])) {
			*f[i] = *fd[i];
		}
	}

	utils_truncate_number(&c->hybrid_duty_limit, 0.05, 1.0);
	utils_truncate_number(&c->hybrid_band, 0.02, 0.6);
	utils_truncate_number(&c->stop_erpm, 0.0, 10000.0);
	utils_truncate_number(&c->brake_taper_start_erpm, 0.0, 30000.0);
	utils_truncate_number(&c->brake_taper_min, 0.0, 1.0);
	utils_truncate_number(&c->rev_cur_max, 0.0, 1.0);
	utils_truncate_number(&c->rev_cur_duty_max, 0.02, 1.0);
	utils_truncate_number(&c->rev_band, 0.02, 0.6);
	utils_truncate_number(&c->rev_duty_max, 0.02, 1.0);
	utils_truncate_number(&c->smart_cur_max, 0.0, 1.0);
	utils_truncate_number(&c->smart_duty_max, 0.02, 1.0);
	utils_truncate_number(&c->smart_ramp, 0.01, 20.0);
	utils_truncate_number(&c->tractor_max_erpm, 0.0, 30000.0);
	utils_truncate_number(&c->tractor_freedom, 0.0, 3600.0);
	utils_truncate_number(&c->tractor_stiffness, 0.0, 10.0);
	utils_truncate_number(&c->tractor_brake, 0.0, 200.0);
	utils_truncate_number(&c->tractor_max_current, 0.0, 500.0);
	utils_truncate_number(&c->tractor_max_lag, 60.0, 36000.0);
	utils_truncate_number(&c->tractor_duty_stiffness, 0.0, 0.1);
	utils_truncate_number(&c->tractor_duty_brake, 0.0, 1.0);
	utils_truncate_number(&c->tractor_max_duty, 0.02, 1.0);

	c->basic_duty_enable = c->basic_duty_enable != 0;
	c->hybrid_enable = c->hybrid_enable != 0;
	c->smart_repress = c->smart_repress != 0;
	if (c->braking_mode > 1) c->braking_mode = 0;
	if (c->reverse_mode > 3) c->reverse_mode = 0;
	if (c->tractor_mode > 2) c->tractor_mode = 0;
	if (c->tractor_speed_mode > 1) c->tractor_speed_mode = 0;
}

static void apply(const rfp_config *c) {
	m_conf = *c;
	m_basic_duty = c->basic_duty_enable;
}

static bool store(const rfp_config *c) {
	uint8_t buf[EE_WORDS * 4];
	memset(buf, 0, sizeof(buf));
	int32_t len = rfp_conf_serialize(buf + 4, c);
	uint32_t head = EE_MAGIC | (uint32_t)len;
	memcpy(buf, &head, 4);

	for (int i = 0;i < EE_WORDS;i++) {
		eeprom_var v;
		memcpy(&v.as_u32, buf + 4 * i, 4);
		if (!conf_general_store_eeprom_var_custom(&v, EE_BASE + i)) {
			return false;
		}
	}

	return true;
}

static bool load(rfp_config *c) {
	uint8_t buf[EE_WORDS * 4];

	for (int i = 0;i < EE_WORDS;i++) {
		eeprom_var v;
		if (!conf_general_read_eeprom_var_custom(&v, EE_BASE + i)) {
			return false;
		}
		memcpy(buf + 4 * i, &v.as_u32, 4);
	}

	uint32_t head;
	memcpy(&head, buf, 4);
	if ((head & 0xFFFF0000) != EE_MAGIC || (head & 0xFFFF) != RFP_CONF_SER_SIZE) {
		return false;
	}

	// The signature check rejects settings saved by another version of the page
	return rfp_conf_deserialize(buf + 4, c);
}

// Custom configuration callbacks (VESC Tool page)
static int get_cfg(uint8_t *data, bool is_default) {
	rfp_config c = m_conf;
	if (is_default) {
		rfp_conf_set_defaults(&c);
	}
	return rfp_conf_serialize(data, &c);
}

static bool set_cfg(uint8_t *data) {
	rfp_config c;
	rfp_conf_set_defaults(&c);

	if (!rfp_conf_deserialize(data, &c)) {
		return false;
	}

	sanitize(&c);
	apply(&c);
	return store(&c);
}

static int get_cfg_xml(uint8_t **data) {
	*data = (uint8_t*)rfp_conf_xml_data;
	return RFP_CONF_XML_SIZE;
}

// ---------------------------------------------------------------- tractor
void rfp_ext_tractor_update(float dt) {
	const rfp_config *c = &m_conf;

	if (c->tractor_mode == 0 || !m_tr_req_valid || UTILS_AGE_S(m_tr_req_time) > TRACTOR_TIMEOUT) {
		m_tr_active = false;
		return;
	}

	const volatile mc_configuration *cfg = mc_interface_get_configuration();
	const float pos = (float)mc_interface_get_tachometer_value(false) * 60.0;	// electrical degrees
	const float rpm = mc_interface_get_rpm();
	const float req = m_tr_req;

	if (!m_tr_active) {
		m_tr_active = true;
		m_tr_anchor = pos;
	}

	// Anchor speed from the throttle
	float target_erpm = 0.0;
	if (c->tractor_speed_mode == 1) {
		if (fabsf(req) > 0.05) {
			target_erpm = req > 0.0 ? c->tractor_max_erpm : -c->tractor_max_erpm;
		}
	} else {
		target_erpm = req * c->tractor_max_erpm;
	}

	m_tr_anchor += target_erpm / 60.0 * 360.0 * dt;
	utils_truncate_number(&m_tr_anchor, pos - c->tractor_max_lag, pos + c->tractor_max_lag);

	// Spring beyond the free play, brake against the speed difference
	const float err = pos - m_tr_anchor;
	const float beyond = fmaxf(fabsf(err) - c->tractor_freedom, 0.0);
	const float spring_sign = err > 0.0 ? -1.0 : 1.0;
	const float speed_err_k = (rpm - target_erpm) / 1000.0;

	if (c->tractor_mode == 1) {
		float i = spring_sign * c->tractor_stiffness * beyond - c->tractor_brake * speed_err_k;
		float i_max = fminf(c->tractor_max_current, cfg->lo_current_max);
		utils_truncate_number(&i, -i_max, i_max);
		mc_interface_set_current(i);
		mc_interface_set_current_off_delay(0.1);	// keep the bridge on to hold and brake
	} else {
		float d = spring_sign * c->tractor_duty_stiffness * beyond - c->tractor_duty_brake * speed_err_k;
		float d_max = c->tractor_max_duty * cfg->l_max_duty;
		utils_truncate_number(&d, -d_max, d_max);
		mc_interface_set_duty(d);
	}
}

static THD_WORKING_AREA(rfp_thread_wa, 1024);
static THD_FUNCTION(rfp_thread, arg) {
	(void)arg;
	chRegSetThreadName("RFP Extras");

	systime_t last = chVTGetSystemTimeX();
	for (;;) {
		chThdSleepMilliseconds(1);
		float dt = UTILS_AGE_S(last);
		last = chVTGetSystemTimeX();
		if (dt > 0.05) {
			dt = 0.05;
		}
		rfp_ext_tractor_update(dt);
	}
}

void rfp_ext_init(void) {
	rfp_config c;
	rfp_conf_set_defaults(&c);

	if (load(&c)) {
		sanitize(&c);
	} else {
		rfp_conf_set_defaults(&c);
	}

	apply(&c);
	conf_custom_add_builtin_config(get_cfg, set_cfg, get_cfg_xml);
	chThdCreateStatic(rfp_thread_wa, sizeof(rfp_thread_wa), NORMALPRIO, rfp_thread, NULL);
}

bool rfp_ext_basic_duty(void) {
	return m_basic_duty;
}

// ---------------------------------------------------------------- relative current requests
bool rfp_ext_current_rel(float *val) {
	const rfp_config *c = &m_conf;
	const float v = *val;

	// Tractor: the throttle only drives the anchor, the tractor thread controls the motor
	if (c->tractor_mode != 0) {
		m_tr_req = v;
		m_tr_req_time = chVTGetSystemTimeX();
		m_tr_req_valid = true;
		return true;
	}

	const volatile mc_configuration *cfg = mc_interface_get_configuration();
	const float rpm = mc_interface_get_rpm();
	const float duty = mc_interface_get_duty_cycle_now();

	if (v >= REV_REQ) {
		// Forward or neutral: reset the reverse handling
		m_smart_cap = 0.0;
		m_smart_last = chVTGetSystemTimeX();
		m_was_braking = false;
		if (fabsf(v) < 0.001) {
			m_need_neutral = false;
		}

		if (c->hybrid_enable && v > 0.001) {
			// Duty limit proportional to the request; the current tapers off over a band below it
			const float lim = v * c->hybrid_duty_limit * cfg->l_max_duty;
			const float band = fmaxf(c->hybrid_band * lim, 0.002);
			float f = (lim - duty) / band;
			utils_truncate_number(&f, 0.0, 1.0);
			*val = v * f;
		}
		return false;
	}

	// Reverse request (v < 0)
	if (rpm > c->stop_erpm) {
		// Still rolling forward: braking
		m_was_braking = true;
		m_smart_cap = 0.0;
		m_smart_last = chVTGetSystemTimeX();

		if (c->braking_mode == 1) {
			mc_interface_set_brake_current_rel(-v);
			return true;
		}

		// Active: official negative current, reduced when approaching the stop
		if (c->brake_taper_min < 0.999 && rpm < c->brake_taper_start_erpm &&
				c->brake_taper_start_erpm > c->stop_erpm) {
			float f = (rpm - c->stop_erpm) / (c->brake_taper_start_erpm - c->stop_erpm);
			utils_truncate_number(&f, 0.0, 1.0);
			*val = v * (c->brake_taper_min + (1.0 - c->brake_taper_min) * f);
		}
		return false;
	}

	// Stopped or already reversing
	if (m_was_braking && c->reverse_mode == 3 && c->smart_repress) {
		m_need_neutral = true;
	}
	m_was_braking = false;

	switch (c->reverse_mode) {
	case 1: // None
		*val = 0.0;
		return false;

	case 2: // Duty
		mc_interface_set_duty(v * c->rev_duty_max * cfg->l_max_duty);
		return true;

	case 3: { // Smart
		if (m_need_neutral) {
			*val = 0.0;
			return false;
		}

		float dt = UTILS_AGE_S(m_smart_last);
		m_smart_last = chVTGetSystemTimeX();
		if (dt > 0.1 || dt < 0.0) {
			dt = 0.001;
		}

		const float cap_max = c->smart_duty_max * cfg->l_max_duty;
		const float cap_goal = -v * cap_max;
		if (m_smart_cap < cap_goal) {
			m_smart_cap = fminf(m_smart_cap + cap_max * dt / c->smart_ramp, cap_goal);
		} else {
			m_smart_cap = cap_goal;
		}

		const float band = fmaxf(c->rev_band * m_smart_cap, 0.002);
		float f = (m_smart_cap - (-duty)) / band;
		utils_truncate_number(&f, 0.0, 1.0);
		*val = v * c->smart_cur_max * f;
		return false;
	}

	default: { // Current (official with the default limits)
		float out = v * c->rev_cur_max;
		if (c->rev_cur_duty_max < 0.999) {
			const float lim = c->rev_cur_duty_max * cfg->l_max_duty;
			const float band = fmaxf(c->rev_band * lim, 0.002);
			float f = (lim - (-duty)) / band;
			utils_truncate_number(&f, 0.0, 1.0);
			out *= f;
		}
		*val = out;
		return false;
	}
	}
}
