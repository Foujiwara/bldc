/*
	RFP extras for the VESC firmware. See rfp_ext.h.

	Relative current requests (mc_interface_set_current_rel) are stored when a function that
	needs them is enabled (hybrid, braking / reverse, tractor). The RFP thread then computes
	the motor command from the last stored request every millisecond, until a new request
	arrives. Any other motor command (brake, duty, current, release, timeout...) cancels the
	stored request at once (rfp_ext_cancel, called from mc_interface).

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

typedef enum {
	PH_FWD = 0,		// forward or neutral request
	PH_BRAKE,		// reverse request while rolling forward
	PH_REV			// reverse request, stopped or reversing
} phase_t;

static rfp_config m_conf;
static volatile bool m_basic_duty = false;
static volatile int m_adc_ctrl = RFP_ADC_CTRL_APP;
static thread_t *m_thread = NULL;

// Stored request
static volatile float m_req = 0.0;
static volatile bool m_req_active = false;
static volatile bool m_req_new = false;

// Braking / reverse state
static phase_t m_phase = PH_FWD;
static float m_smart_cap = 0.0;			// smart reverse duty limit (ramped)
static bool m_need_neutral = false;		// re-press to reverse
static float m_last_out = 0.0;			// last relative current sent (0 after brake or duty)
static float m_trans_from = 0.0;
static float m_trans_left = 0.0;

// Tractor state
static bool m_tr_active = false;
static float m_tr_anchor = 0.0;			// electrical degrees

static void sanitize(rfp_config *c) {
	float *f[] = { &c->hybrid_duty_limit, &c->hybrid_band, &c->stop_erpm, &c->stop_hyst, &c->rev_transition,
			&c->brake_taper_start_erpm, &c->brake_taper_min, &c->rev_cur_max, &c->rev_cur_duty_max, &c->rev_band,
			&c->rev_duty_max, &c->smart_cur_max, &c->smart_duty_max, &c->smart_ramp, &c->tractor_max_erpm,
			&c->tractor_freedom, &c->tractor_stiffness, &c->tractor_brake, &c->tractor_max_current,
			&c->tractor_max_lag, &c->tractor_duty_stiffness, &c->tractor_duty_brake, &c->tractor_max_duty };
	rfp_config d;
	rfp_conf_set_defaults(&d);
	float *fd[] = { &d.hybrid_duty_limit, &d.hybrid_band, &d.stop_erpm, &d.stop_hyst, &d.rev_transition,
			&d.brake_taper_start_erpm, &d.brake_taper_min, &d.rev_cur_max, &d.rev_cur_duty_max, &d.rev_band,
			&d.rev_duty_max, &d.smart_cur_max, &d.smart_duty_max, &d.smart_ramp, &d.tractor_max_erpm,
			&d.tractor_freedom, &d.tractor_stiffness, &d.tractor_brake, &d.tractor_max_current,
			&d.tractor_max_lag, &d.tractor_duty_stiffness, &d.tractor_duty_brake, &d.tractor_max_duty };
	for (unsigned i = 0;i < sizeof(f) / sizeof(f[0]);i++) {
		if (!isfinite(*f[i])) {
			*f[i] = *fd[i];
		}
	}

	utils_truncate_number(&c->hybrid_duty_limit, 0.05, 1.0);
	utils_truncate_number(&c->hybrid_band, 0.02, 0.6);
	utils_truncate_number(&c->stop_erpm, 0.0, 10000.0);
	utils_truncate_number(&c->stop_hyst, 0.0, 3.0);
	utils_truncate_number(&c->rev_transition, 0.0, 2.0);
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
	c->braking_reverse_enable = c->braking_reverse_enable != 0;
	c->smart_repress = c->smart_repress != 0;
	c->tractor_enable = c->tractor_enable != 0;
	if (c->braking_mode > 1) c->braking_mode = 0;
	if (c->reverse_mode > 3) c->reverse_mode = 0;
	if (c->tractor_mode > 1) c->tractor_mode = 0;
	if (c->tractor_speed_mode > 1) c->tractor_speed_mode = 0;
	if (c->adc_ctrl_type > 1) c->adc_ctrl_type = 0;
}

static bool rel_functions_on(const rfp_config *c) {
	return c->hybrid_enable || c->braking_reverse_enable || c->tractor_enable;
}

static void apply(const rfp_config *c) {
	m_conf = *c;
	m_basic_duty = c->basic_duty_enable;
	m_adc_ctrl = c->adc_ctrl_type;
	if (!rel_functions_on(c)) {
		m_req_active = false;
	}
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

// ---------------------------------------------------------------- outputs
// Relative current to the motor, as mc_interface_set_current_rel does. req: the request, which
// keeps the bridge on while it is not zero (even if the output is zero).
static void out_current_rel(float out, float req) {
	const volatile mc_configuration *cfg = mc_interface_get_configuration();
	const float duty = mc_interface_get_duty_cycle_now();

	if (fabsf(duty) < 0.02 || SIGN(out) == SIGN(duty)) {
		mc_interface_set_current(out * cfg->lo_current_max);
	} else {
		mc_interface_set_current(out * fabsf(cfg->lo_current_min));
	}

	if (fabsf(req * cfg->l_abs_current_max) > cfg->cc_min_current) {
		mc_interface_set_current_off_delay(0.1);
	}

	m_last_out = out;
}

// Taper factor: 1 below the limit minus the band, 0 at the limit
static float taper(float lim, float band_frac, float duty_dir) {
	const float band = fmaxf(band_frac * lim, 0.002);
	float f = (lim - duty_dir) / band;
	utils_truncate_number(&f, 0.0, 1.0);
	return f;
}

// Brake / reverse transition: blend from the value at the switch to the new target
static float blend(float target, float dt) {
	if (m_trans_left <= 0.0 || m_conf.rev_transition <= 0.0) {
		return target;
	}
	m_trans_left -= dt;
	float k = 1.0 - m_trans_left / m_conf.rev_transition;
	utils_truncate_number(&k, 0.0, 1.0);
	return m_trans_from + (target - m_trans_from) * k;
}

static void set_phase(phase_t ph) {
	if (ph == m_phase) {
		return;
	}
	if ((m_phase == PH_BRAKE && ph == PH_REV) || (m_phase == PH_REV && ph == PH_BRAKE)) {
		m_trans_from = m_last_out;
		m_trans_left = m_conf.rev_transition;
		if (ph == PH_REV && m_conf.reverse_mode == 3 && m_conf.smart_repress) {
			m_need_neutral = true;
		}
	} else {
		m_trans_left = 0.0;
	}
	if (ph != PH_REV) {
		m_smart_cap = 0.0;
	}
	m_phase = ph;
}

// ---------------------------------------------------------------- tractor
static void tractor_step(float dt, float req) {
	const rfp_config *c = &m_conf;
	const volatile mc_configuration *cfg = mc_interface_get_configuration();
	const float pos = (float)mc_interface_get_tachometer_value(false) * 60.0;	// electrical degrees
	const float rpm = mc_interface_get_rpm();

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

	if (c->tractor_mode == 0) {
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

// ---------------------------------------------------------------- continuous computation
void rfp_ext_update(float dt) {
	const rfp_config *c = &m_conf;

	if (!m_req_active || !rel_functions_on(c)) {
		m_tr_active = false;
		return;
	}

	const float v = m_req;
	if (m_req_new) {
		m_req_new = false;
		if (fabsf(v) < 0.001) {
			m_need_neutral = false;
		}
	}

	if (c->tractor_enable) {
		tractor_step(dt, v);
		return;
	}
	m_tr_active = false;

	const volatile mc_configuration *cfg = mc_interface_get_configuration();
	const float duty = mc_interface_get_duty_cycle_now();

	// Forward or neutral request
	if (v >= REV_REQ) {
		set_phase(PH_FWD);
		float out = v;
		if (c->hybrid_enable && v > 0.001) {
			out = v * taper(v * c->hybrid_duty_limit * cfg->l_max_duty, c->hybrid_band, duty);
		}
		out_current_rel(out, v);
		return;
	}

	// Reverse request, handling off: official negative current
	if (!c->braking_reverse_enable) {
		set_phase(PH_FWD);
		out_current_rel(v, v);
		return;
	}

	// Braking or reverse, with hysteresis around Stop ERPM
	const float rpm = mc_interface_get_rpm();
	if (m_phase == PH_REV) {
		if (rpm > c->stop_erpm * (1.0 + c->stop_hyst)) {
			set_phase(PH_BRAKE);
		}
	} else {
		set_phase(rpm > c->stop_erpm ? PH_BRAKE : PH_REV);
	}

	if (m_phase == PH_BRAKE) {
		if (c->braking_mode == 1) {
			mc_interface_set_brake_current_rel(-v);
			m_last_out = 0.0;
			return;
		}

		// Active: official negative current, reduced when approaching the stop
		float out = v;
		if (c->brake_taper_min < 0.999 && rpm < c->brake_taper_start_erpm &&
				c->brake_taper_start_erpm > c->stop_erpm) {
			float f = (rpm - c->stop_erpm) / (c->brake_taper_start_erpm - c->stop_erpm);
			utils_truncate_number(&f, 0.0, 1.0);
			out = v * (c->brake_taper_min + (1.0 - c->brake_taper_min) * f);
		}
		out_current_rel(blend(out, dt), v);
		return;
	}

	// PH_REV: stopped or reversing
	switch (c->reverse_mode) {
	case 1: // None
		out_current_rel(blend(0.0, dt), v);
		break;

	case 2: // Duty
		mc_interface_set_duty(v * c->rev_duty_max * cfg->l_max_duty);
		m_last_out = 0.0;
		break;

	case 3: { // Smart
		if (m_need_neutral) {
			out_current_rel(blend(0.0, dt), v);
			break;
		}

		const float cap_max = c->smart_duty_max * cfg->l_max_duty;
		const float cap_goal = -v * cap_max;
		if (m_smart_cap < cap_goal) {
			m_smart_cap = fminf(m_smart_cap + cap_max * dt / c->smart_ramp, cap_goal);
		} else {
			m_smart_cap = cap_goal;
		}

		const float out = v * c->smart_cur_max * taper(m_smart_cap, c->rev_band, -duty);
		out_current_rel(blend(out, dt), v);
	} break;

	default: { // Current (official with the default limits)
		float out = v * c->rev_cur_max;
		if (c->rev_cur_duty_max < 0.999) {
			out *= taper(c->rev_cur_duty_max * cfg->l_max_duty, c->rev_band, -duty);
		}
		out_current_rel(blend(out, dt), v);
	} break;
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
		rfp_ext_update(dt);
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
	m_thread = chThdCreateStatic(rfp_thread_wa, sizeof(rfp_thread_wa), NORMALPRIO, rfp_thread, NULL);
}

bool rfp_ext_basic_duty(void) {
	return m_basic_duty;
}

int rfp_ext_adc_ctrl(void) {
	return m_adc_ctrl;
}

float rfp_ext_adc_signed(float adc1, float adc2, float deadband) {
	utils_truncate_number(&adc1, 0.0, 1.0);
	utils_truncate_number(&adc2, 0.0, 1.0);
	// Brake priority: ADC2 pressed cancels the throttle
	return adc2 > deadband ? -adc2 : adc1;
}

bool rfp_ext_current_rel(float *val) {
	if (!rel_functions_on(&m_conf)) {
		return false;
	}

	if (!m_req_active) {
		// New sequence: start from a clean state
		m_phase = PH_FWD;
		m_smart_cap = 0.0;
		m_trans_left = 0.0;
		m_need_neutral = false;
		m_last_out = 0.0;
		m_tr_active = false;
	}

	m_req = *val;
	m_req_new = true;
	m_req_active = true;
	return true;
}

void rfp_ext_cancel(void) {
	// Commands sent by the RFP thread itself do not cancel the stored request
	if (m_thread != NULL && chThdGetSelfX() == m_thread) {
		return;
	}
	m_req_active = false;
}
