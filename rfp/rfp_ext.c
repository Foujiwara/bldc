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

// Saved settings use 7 emulated EEPROM words at the top of the custom range (0 to 127), which
// LispBM scripts rarely use: magic, flags, then the five float values.
#define EE_BASE			121
#define EE_MAGIC		0x52465001
#define EE_WORDS		7

static rfp_config m_conf;
static volatile bool m_basic_duty = false;

// Smart reverse state
static float m_sr_duty = 0.0;
static systime_t m_sr_last = 0;

static void sanitize(rfp_config *c) {
	if (!isfinite(c->hybrid_duty_limit)) c->hybrid_duty_limit = 1.0;
	if (!isfinite(c->hybrid_band)) c->hybrid_band = 0.15;
	if (!isfinite(c->smart_rev_erpm)) c->smart_rev_erpm = 1000.0;
	if (!isfinite(c->smart_rev_max_duty)) c->smart_rev_max_duty = 0.07;
	if (!isfinite(c->smart_rev_ramp)) c->smart_rev_ramp = 3.0;

	utils_truncate_number(&c->hybrid_duty_limit, 0.05, 1.0);
	utils_truncate_number(&c->hybrid_band, 0.02, 0.6);
	utils_truncate_number(&c->smart_rev_erpm, 0.0, 30000.0);
	utils_truncate_number(&c->smart_rev_max_duty, 0.0, 1.0);
	utils_truncate_number(&c->smart_rev_ramp, 0.01, 20.0);
	c->basic_duty_enable = c->basic_duty_enable != 0;
	c->hybrid_enable = c->hybrid_enable != 0;
	c->smart_rev_enable = c->smart_rev_enable != 0;
}

static void apply(const rfp_config *c) {
	m_conf = *c;
	m_basic_duty = c->basic_duty_enable;
}

static bool store(const rfp_config *c) {
	uint32_t w[EE_WORDS];
	float f[5] = { c->hybrid_duty_limit, c->hybrid_band, c->smart_rev_erpm, c->smart_rev_max_duty, c->smart_rev_ramp };

	w[0] = EE_MAGIC;
	w[1] = (c->basic_duty_enable ? 1 : 0) | (c->hybrid_enable ? 2 : 0) | (c->smart_rev_enable ? 4 : 0);
	for (int i = 0;i < 5;i++) {
		memcpy(&w[2 + i], &f[i], sizeof(float));
	}

	for (int i = 0;i < EE_WORDS;i++) {
		eeprom_var v;
		v.as_u32 = w[i];
		if (!conf_general_store_eeprom_var_custom(&v, EE_BASE + i)) {
			return false;
		}
	}

	return true;
}

static bool load(rfp_config *c) {
	uint32_t w[EE_WORDS];

	for (int i = 0;i < EE_WORDS;i++) {
		eeprom_var v;
		if (!conf_general_read_eeprom_var_custom(&v, EE_BASE + i)) {
			return false;
		}
		w[i] = v.as_u32;
	}

	if (w[0] != EE_MAGIC) {
		return false;
	}

	float f[5];
	for (int i = 0;i < 5;i++) {
		memcpy(&f[i], &w[2 + i], sizeof(float));
	}

	c->basic_duty_enable = (w[1] & 1) != 0;
	c->hybrid_enable = (w[1] & 2) != 0;
	c->smart_rev_enable = (w[1] & 4) != 0;
	c->hybrid_duty_limit = f[0];
	c->hybrid_band = f[1];
	c->smart_rev_erpm = f[2];
	c->smart_rev_max_duty = f[3];
	c->smart_rev_ramp = f[4];
	return true;
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
}

bool rfp_ext_basic_duty(void) {
	return m_basic_duty;
}

bool rfp_ext_current_rel(float *val) {
	const rfp_config *c = &m_conf;

	if (!c->smart_rev_enable && !c->hybrid_enable) {
		return false;
	}

	const volatile mc_configuration *cfg = mc_interface_get_configuration();
	const float duty = mc_interface_get_duty_cycle_now();
	const float v = *val;

	if (c->smart_rev_enable) {
		if (v < -0.001) {
			if (mc_interface_get_rpm() > c->smart_rev_erpm) {
				// Rolling forward: a reverse request brakes
				m_sr_duty = duty;
				mc_interface_set_brake_current_rel(-v);
				return true;
			}

			// Slow or already reversing: reverse at a limited duty cycle, reached with a ramp
			systime_t now = chVTGetSystemTimeX();
			float dt = UTILS_AGE_S(m_sr_last);
			m_sr_last = now;
			if (dt > 0.1 || dt <= 0.0) {
				// First request after a pause: start the ramp from the real duty
				m_sr_duty = duty;
				dt = 0.001;
			}

			float max_duty = fminf(c->smart_rev_max_duty, cfg->l_max_duty);
			utils_step_towards(&m_sr_duty, v * max_duty, max_duty * dt / c->smart_rev_ramp);
			mc_interface_set_duty(m_sr_duty);
			return true;
		}

		// Not requesting reverse: follow the real duty so a new reverse request starts from it
		m_sr_duty = duty;
		m_sr_last = chVTGetSystemTimeX();
	}

	if (c->hybrid_enable && fabsf(v) > 0.001) {
		// Duty limit proportional to the request; the current tapers off over a band below it
		const float lim = fabsf(v) * c->hybrid_duty_limit * cfg->l_max_duty;
		const float band = fmaxf(c->hybrid_band * lim, 0.002);
		const float d = duty * (v > 0.0 ? 1.0 : -1.0);
		float f = (lim - d) / band;
		utils_truncate_number(&f, 0.0, 1.0);
		*val = v * f;
	}

	return false;
}
