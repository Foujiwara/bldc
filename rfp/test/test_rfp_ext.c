// Host test of the RFP extras logic (stored request + continuous computation: hybrid, braking /
// reverse, tractor, storage). Run: bash rfp/test/run_tests.sh
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "rfp_ext.h"
#include "rfp_conf_gen.h"
#include "conf_custom.h"
#include "conf_general.h"
#include "mc_interface.h"
#include "ch.h"

systime_t stub_time;
thread_t stub_rfp_thread;
thread_t stub_app_thread;
thread_t *stub_self = &stub_app_thread;

// ---- stubs of the firmware services (commands from the RFP thread are recorded)
static mc_configuration m_cfg = { .l_max_duty = 0.95f, .lo_current_max = 60.0f, .lo_current_min = -40.0f,
		.l_abs_current_max = 150.0f, .cc_min_current = 0.05f };
static float m_duty, m_rpm;
static int m_tacho;
static float m_brake_rel, m_set_duty, m_set_current;
static int m_brake_calls, m_duty_calls, m_current_calls;

const volatile mc_configuration* mc_interface_get_configuration(void) { return &m_cfg; }
float mc_interface_get_duty_cycle_now(void) { return m_duty; }
float mc_interface_get_rpm(void) { return m_rpm; }
float mc_interface_get_input_voltage_filtered(void) { return 48.0f; }
int mc_interface_get_tachometer_value(_Bool reset) { (void)reset; return m_tacho; }
void mc_interface_set_brake_current_rel(float val) { m_brake_rel = val; m_brake_calls++; rfp_ext_cancel(); }
void mc_interface_set_duty(float duty) { m_set_duty = duty; m_duty_calls++; rfp_ext_cancel(); }
void mc_interface_set_current(float current) { m_set_current = current; m_current_calls++; rfp_ext_cancel(); }
void mc_interface_set_current_off_delay(float d) { (void)d; }

static uint32_t m_ee[128];
static bool m_ee_valid[128];
bool conf_general_read_eeprom_var_custom(eeprom_var *v, int a) {
	if (a < 0 || a >= 128 || !m_ee_valid[a]) return false;
	v->as_u32 = m_ee[a];
	return true;
}
bool conf_general_store_eeprom_var_custom(eeprom_var *v, int a) {
	if (a < 0 || a >= 128) return false;
	m_ee[a] = v->as_u32;
	m_ee_valid[a] = true;
	return true;
}

static int (*g_get)(uint8_t*, bool);
static bool (*g_set)(uint8_t*);
static int (*g_xml)(uint8_t**);
void conf_custom_add_builtin_config(int (*get_cfg)(uint8_t*, bool), bool (*set_cfg)(uint8_t*), int (*get_xml)(uint8_t**)) {
	g_get = get_cfg; g_set = set_cfg; g_xml = get_xml;
}

static int fails;
static void check(const char *w, int ok) { printf("  [%s] %s\n", ok ? " OK " : "ECHEC", w); if (!ok) fails++; }
static int near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

static void configure(const rfp_config *c) {
	uint8_t b[160];
	rfp_conf_serialize(b, c);
	g_set(b);
}

// a relative current request from an app (returns true if stored by RFP)
static bool req(float v) {
	stub_self = &stub_app_thread;
	float x = v;
	return rfp_ext_current_rel(&x);
}

// n steps of 1 ms of the RFP thread
static void run(int n) {
	stub_self = &stub_rfp_thread;
	for (int i = 0;i < n;i++) {
		stub_time += 1;
		rfp_ext_update(0.001f);
	}
	stub_self = &stub_app_thread;
}

// relative current of the last current command, with the rule of mc_interface_set_current_rel:
// positive limit when the duty is near zero or has the same sign, braking limit otherwise
static float rel_out(void) {
	bool same = fabsf(m_duty) < 0.02f || (m_set_current < 0.0f) == (m_duty < 0.0f);
	return m_set_current / (same ? m_cfg.lo_current_max : -m_cfg.lo_current_min);
}

int main(void) {
	rfp_config c;

	printf("== Defaults: identical to the official firmware\n");
	rfp_ext_init();
	check("nothing enabled: request not stored (official path)", !req(0.7f) && !req(-0.7f));
	check("Basic Duty Mode off", !rfp_ext_basic_duty());
	uint8_t *xml;
	check("custom page registered, XML available", g_xml && g_xml(&xml) == RFP_CONF_XML_SIZE && xml[4] == 0x78);

	printf("== Continuous computation (hybrid, max duty 0.95, band 0.2)\n");
	rfp_conf_set_defaults(&c);
	c.hybrid_enable = 1; c.hybrid_band = 0.2f;
	configure(&c);
	float lim = 0.5f * 0.95f;
	m_rpm = 0; m_duty = 0.0f; m_current_calls = 0;
	check("request stored", req(0.5f));
	run(10);
	check("one command per millisecond from the last request", m_current_calls == 10 && near(rel_out(), 0.5f, 1e-5));
	m_duty = lim * 0.9f; run(30);
	check("duty rises without a new request: current reduced (half way in the band)", near(rel_out(), 0.25f, 0.01));
	m_duty = lim; run(100);
	check("at the duty limit: zero current", near(m_set_current, 0.0f, 0.01f));
	m_duty = lim * 0.95f; run(30);
	check("small drop below the limit: small current (smooth, no all-or-nothing)", rel_out() > 0.0f && rel_out() < 0.15f);

	printf("== Another command takes over\n");
	m_current_calls = 0;
	stub_self = &stub_app_thread;
	mc_interface_set_brake_current_rel(0.5f);      // e.g. ADC2 brake from the app
	run(5);
	check("brake from the app: RFP stops commanding", m_current_calls == 0);
	req(0.3f); run(2);
	check("new request: RFP resumes", m_current_calls == 2);

	printf("== Braking / reverse switch\n");
	rfp_conf_set_defaults(&c);
	c.hybrid_enable = 1;                           // only to have the request stored
	configure(&c);
	m_rpm = 3000; m_duty = 0.3f;
	req(-0.6f); run(1);
	check("Braking / Reverse Off: official negative current", near(rel_out(), -0.6f, 1e-5));

	rfp_conf_set_defaults(&c);
	c.braking_reverse_enable = 1; c.braking_mode = 1;
	configure(&c);
	m_brake_calls = 0;
	req(-0.8f); run(1);
	check("Regen Only: brake current with the request", m_brake_calls == 1 && near(m_brake_rel, 0.8f, 1e-5));

	rfp_conf_set_defaults(&c);
	c.braking_reverse_enable = 1; c.brake_taper_start_erpm = 1500; c.brake_taper_min = 0.3f; c.stop_erpm = 300;
	c.rev_transition = 0.0f;
	configure(&c);
	m_rpm = 3000; req(-1.0f); run(1);
	check("Active above the taper: full negative current", near(rel_out(), -1.0f, 1e-5));
	m_rpm = 900; run(1);
	check("Active in the taper: reduced (0.65 at half way)", near(rel_out(), -0.65f, 0.01));

	printf("== Hysteresis and transition (Stop ERPM 300, hysteresis 50 %%, transition 0.2 s)\n");
	rfp_conf_set_defaults(&c);
	c.braking_reverse_enable = 1; c.reverse_mode = 0; c.rev_cur_max = 0.4f; c.stop_erpm = 300;
	c.stop_hyst = 0.5f; c.rev_transition = 0.2f;
	configure(&c);
	m_rpm = 1000; m_duty = 0.1f;
	req(-1.0f); run(5);
	check("braking: full negative current", near(rel_out(), -1.0f, 1e-5));
	m_rpm = 250; m_duty = 0.0f; run(100);
	check("below Stop ERPM: half way of the transition to the reverse current", near(rel_out(), -0.7f, 0.02));
	run(150);
	check("after the transition: reverse current (40 %)", near(rel_out(), -0.4f, 1e-4));
	m_rpm = 400; run(5);
	check("400 ERPM: still reversing (hysteresis up to 450)", near(rel_out(), -0.4f, 1e-4));
	m_rpm = 500; run(400);
	check("500 ERPM: braking again", near(rel_out(), -1.0f, 1e-4));

	printf("== Reverse modes\n");
	rfp_conf_set_defaults(&c);
	c.braking_reverse_enable = 1; c.reverse_mode = 1; c.rev_transition = 0.0f;
	configure(&c);
	m_rpm = 0; m_duty = 0.0f; req(-1.0f); run(1);
	check("None: zero current at stop", near(m_set_current, 0.0f, 1e-6));

	rfp_conf_set_defaults(&c);
	c.braking_reverse_enable = 1; c.reverse_mode = 0; c.rev_cur_max = 0.4f; c.rev_duty_max = 0.2f;
	c.rev_band = 0.2f; c.rev_transition = 0.0f;
	configure(&c);
	m_rpm = -2000; m_duty = -0.2f * 0.95f; req(-1.0f); run(100);
	check("Current: zero at the reverse duty limit", near(m_set_current, 0.0f, 0.01f));

	rfp_conf_set_defaults(&c);
	c.braking_reverse_enable = 1; c.reverse_mode = 2; c.rev_duty_max = 0.3f;
	configure(&c);
	m_rpm = 0; m_duty = 0.0f; m_duty_calls = 0;
	req(-0.5f); run(3);
	check("Duty: reverse duty = request x Reverse Duty Max, every ms", m_duty_calls == 3 && near(m_set_duty, -0.5f * 0.3f * 0.95f, 1e-5));

	rfp_conf_set_defaults(&c);
	c.braking_reverse_enable = 1; c.reverse_mode = 2; c.rev_duty_max = 0.2f; c.rev_ramp = 2.0f;
	c.rev_transition = 0.0f;
	configure(&c);
	m_rpm = 0; m_duty = 0.0f; req(0.0f); run(1);
	req(-1.0f); run(1000);                       // 1 s of a 2 s ramp
	check("Ramp, Duty: half of the reverse duty after half the ramp", near(m_set_duty, -0.5f * 0.2f * 0.95f, 1e-3));
	req(-0.25f); run(1);
	check("Ramp, Duty: falls at once when the request is reduced", near(m_set_duty, -0.25f * 0.2f * 0.95f, 1e-5));

	rfp_conf_set_defaults(&c);
	c.braking_reverse_enable = 1; c.reverse_mode = 0; c.rev_cur_max = 0.5f; c.rev_ramp = 1.0f;
	c.rev_transition = 0.0f;
	configure(&c);
	m_rpm = 0; m_duty = 0.0f; req(0.0f); run(1);
	req(-1.0f); run(500);
	check("Ramp, Current: half of Reverse Current Max after half the ramp", near(rel_out(), -0.25f, 1e-3));

	printf("== Re-press to Reverse (arm level 95 %%, disarm 2 s)\n");
	rfp_conf_set_defaults(&c);
	c.braking_reverse_enable = 1; c.reverse_mode = 2; c.rev_duty_max = 0.5f; c.rev_repress = 1;
	c.rev_transition = 0.0f;
	configure(&c);
	m_rpm = 0; m_duty = 0.0f; req(0.0f); run(1);
	m_rpm = 3000; m_duty = 0.3f; req(0.5f); run(5);     // rolling forward
	req(-1.0f); run(5);                                  // full brake
	check("braking while rolling", rel_out() < -0.9f);
	m_rpm = 0; m_duty = 0.0f; m_duty_calls = 0; run(200);
	check("stopped, brake still held: no reverse", m_duty_calls == 0 && near(m_set_current, 0.0f, 1e-6));
	req(0.0f); run(100); m_duty_calls = 0;
	req(-0.5f); run(100);
	check("press below Arm Level: no reverse", m_duty_calls == 0);
	req(0.0f); run(100); req(-0.5f); run(100);
	check("... and does not arm", m_duty_calls == 0);
	req(0.0f); run(100); req(-1.0f); run(50);
	check("arming press: no reverse during the press", m_duty_calls == 0);
	req(0.0f); run(500); req(-0.6f); run(5);
	check("armed: next press reverses (duty)", m_duty_calls == 5 && near(m_set_duty, -0.6f * 0.5f * 0.95f, 1e-5));
	req(0.0f); run(1000); m_duty_calls = 0; req(-0.6f); run(5);
	check("neutral 1 s (< 2 s): still armed", m_duty_calls == 5);
	req(0.0f); run(2100); m_duty_calls = 0; req(-0.6f); run(5);
	check("neutral 2.1 s: disarmed", m_duty_calls == 0);
	req(0.0f); run(10); req(-1.0f); run(10); req(0.0f); run(10);   // arm again
	req(0.3f); run(10); req(0.0f); run(10);                        // throttle
	m_duty_calls = 0; req(-0.6f); run(5);
	check("throttle disarms", m_duty_calls == 0);

	printf("== Tractor (spring on the tachometer, 60 deg per step)\n");
	rfp_conf_set_defaults(&c);
	c.tractor_enable = 1; c.tractor_mode = 0; c.tractor_freedom = 120; c.tractor_stiffness = 0.1f;
	c.tractor_brake = 2.0f; c.tractor_max_current = 20; c.tractor_max_erpm = 600; c.tractor_max_lag = 720;
	configure(&c);
	m_rpm = 0; m_tacho = 100; m_current_calls = 0;
	req(0.0f); run(1);
	check("at the anchor: zero current", m_current_calls == 1 && near(m_set_current, 0.0f, 1e-5));
	m_tacho = 102; run(1);
	check("inside the free play: zero current", near(m_set_current, 0.0f, 1e-5));
	m_tacho = 105; run(1);
	check("beyond the free play: pulls back (-18 A)", near(m_set_current, -18.0f, 0.01));
	m_tacho = 110; run(1);
	check("current limited to Tractor Max Current", near(m_set_current, -20.0f, 1e-4));
	m_tacho = 100; m_rpm = 1000; run(1);
	check("brake against the free motion (-2 A at 1000 ERPM)", near(m_set_current, -2.0f, 0.01));
	m_rpm = 0; req(1.0f); run(500);              // 0.5 s at 600 ERPM, anchor held at Max Lag
	check("full throttle: pull forward (capped)", m_set_current > 19.9f);
	m_tacho = 112; m_rpm = 600; run(1);
	check("wheel at the anchor, at its speed: no force", fabsf(m_set_current) < 0.5f);
	m_current_calls = 0;
	stub_self = &stub_app_thread;
	mc_interface_set_brake_current_rel(1.0f);      // brake from the app
	run(5);
	check("another command: tractor stops at once", m_current_calls == 0);

	rfp_conf_set_defaults(&c);
	c.tractor_enable = 1; c.tractor_mode = 1; c.tractor_freedom = 60; c.tractor_duty_stiffness = 0.001f;
	c.tractor_duty_brake = 0.0f; c.tractor_max_duty = 0.5f;
	configure(&c);
	m_rpm = 0; m_tacho = 0; m_duty_calls = 0;
	req(0.0f); run(1);
	m_tacho = -5; run(1);
	check("Spring Duty: duty pulls forward (0.24)", m_duty_calls == 2 && near(m_set_duty, 0.24f, 1e-4));

	printf("== Basic Duty Mode: real release (forward and reverse)\n");
	rfp_conf_set_defaults(&c);
	configure(&c);
	stub_self = &stub_app_thread;
	check("Basic Duty off: duty request not stored (official path)", !rfp_ext_duty(0.3f));
	c.basic_duty_enable = 1;
	configure(&c);
	m_duty = 0.30f; m_duty_calls = 0; m_current_calls = 0;
	check("Basic Duty on: duty request stored", rfp_ext_duty(0.30f));
	run(3);
	check("request = present duty: duty control", m_duty_calls == 3 && near(m_set_duty, 0.30f, 1e-6));
	rfp_ext_duty(0.297f); run(1);
	check("slow release (0.3 % below): no braking clamp needed yet, still duty", near(m_set_duty, 0.297f, 1e-6));
	m_duty = 0.297f; rfp_ext_duty(0.290f); m_current_calls = 0; run(1);
	check("0.7 % below the present duty: released (zero current)", m_current_calls == 1 && near(m_set_current, 0.0f, 1e-6));
	m_duty = 0.20f; rfp_ext_duty(0.25f); m_duty_calls = 0; run(1);
	check("duty fell below the request: duty control again", m_duty_calls == 1 && near(m_set_duty, 0.25f, 1e-6));
	m_duty = 0.25f; rfp_ext_duty(0.0f); m_current_calls = 0; run(5);
	check("zero request: released", m_current_calls == 5 && near(m_set_current, 0.0f, 1e-6));
	m_duty = -0.30f; rfp_ext_duty(-0.20f); m_current_calls = 0; run(1);
	check("reverse, request reduced: released", m_current_calls == 1 && near(m_set_current, 0.0f, 1e-6));
	m_duty = 0.30f; rfp_ext_duty(-0.20f); m_current_calls = 0; run(1);
	check("opposite request while rolling: released (no braking)", m_current_calls == 1);
	m_duty = 0.005f; m_duty_calls = 0; run(1);
	check("... then reverse duty once stopped", m_duty_calls == 1 && near(m_set_duty, -0.20f, 1e-6));
	stub_self = &stub_app_thread;
	mc_interface_set_brake_current_rel(0.5f);
	m_duty_calls = 0; m_current_calls = 0; run(3);
	check("another command: stored duty stops", m_duty_calls == 0 && m_current_calls == 0);

	rfp_conf_set_defaults(&c);
	c.basic_duty_enable = 1; c.braking_reverse_enable = 1; c.reverse_mode = 2; c.rev_duty_max = 0.5f;
	c.rev_transition = 0.0f;
	configure(&c);
	m_rpm = 0; m_duty = 0.0f; req(0.0f); run(1);
	req(-0.6f); run(5);
	m_duty = -0.6f * 0.5f * 0.95f; m_rpm = -3000; run(1);
	req(-0.57f); m_current_calls = 0; run(1);
	check("Reverse Duty, release (1.4 % of duty): released, no braking", m_current_calls == 1 && near(m_set_current, 0.0f, 1e-6));

	printf("== Reverse Current, Duty Limited (max duty 0.95, limit 40 %%, current 50 %%, soft zone 10 %%)\n");
	rfp_conf_set_defaults(&c);
	c.braking_reverse_enable = 1; c.reverse_mode = 3; c.rev_duty_max = 0.4f; c.rev_cur_max = 0.5f;
	c.rev_band = 0.1f; c.rev_transition = 0.0f;
	configure(&c);
	m_rpm = 0; m_duty = 0.0f; req(0.0f); run(1);
	req(-0.2f); run(30);
	check("20 % request at standstill: full reverse current at once", near(rel_out(), -0.5f, 1e-4));
	req(-1.0f); m_duty = -0.2f; m_rpm = -2000; run(50);
	check("far below the limit (0.38): full current", near(rel_out(), -0.5f, 1e-4));
	m_duty = -0.361f; run(50);
	check("in the soft zone (half way): half current", near(rel_out(), -0.25f, 0.01));
	req(-0.5f); run(50);
	check("request reduced, limit below the duty: zero current (released, no braking)", near(m_set_current, 0.0f, 1e-5));
	m_duty = -0.0475f; req(-0.05f); run(50);    // small limit 0.019, duty at 0.0475
	check("small limit: still zero current, no chatter", near(m_set_current, 0.0f, 1e-5));
	m_duty = -0.010f; run(50);
	check("small limit, duty below it: smooth partial current (wide soft zone)", rel_out() < -0.1f && rel_out() > -0.5f);

	m_cfg.foc_motor_flux_linkage = 0.01f; m_cfg.foc_overmod_factor = 1.0f;
	req(-1.0f); m_rpm = -2000; m_duty = -0.39f; run(30);      // speed duty 0.076, applied duty high (R x I)
	check("limit on the speed (back EMF), not on the applied duty: full current", near(rel_out(), -0.5f, 1e-3));
	m_cfg.foc_motor_flux_linkage = 0.0f;

	printf("== ADC control type\n");
	rfp_conf_set_defaults(&c);
	configure(&c);
	check("default: Control Type of the ADC app", rfp_ext_adc_ctrl() == RFP_ADC_CTRL_APP);
	c.adc_ctrl_type = 1;
	configure(&c);
	check("Current Reverse ADC2 selected (Brake Priority on by default)", rfp_ext_adc_ctrl() == RFP_ADC_CTRL_CURRENT_REV_ADC2);
	check("ADC1 alone: forward request", near(rfp_ext_adc_signed(0.7f, 0.0f, 0.15f), 0.7f, 1e-6));
	check("ADC2 alone: reverse request", near(rfp_ext_adc_signed(0.0f, 0.6f, 0.15f), -0.6f, 1e-6));
	check("both pressed: brake wins, throttle cut", near(rfp_ext_adc_signed(1.0f, 0.3f, 0.15f), -0.3f, 1e-6));
	check("ADC2 within the deadband: throttle kept", near(rfp_ext_adc_signed(0.8f, 0.1f, 0.15f), 0.8f, 1e-6));
	c.adc_brake_priority = 0;
	configure(&c);
	check("Brake Priority off: ADC1 - ADC2", near(rfp_ext_adc_signed(1.0f, 0.3f, 0.15f), 0.7f, 1e-6));
	c.adc_ctrl_type = 7;
	configure(&c);
	check("invalid ADC control type sanitized", rfp_ext_adc_ctrl() == RFP_ADC_CTRL_APP);

	printf("== Configuration page and storage\n");
	rfp_conf_set_defaults(&c);
	c.basic_duty_enable = 1; c.braking_reverse_enable = 1; c.reverse_mode = 2; c.braking_mode = 1;
	c.tractor_enable = 1; c.tractor_mode = 1; c.rev_ramp = 5.0f; c.tractor_freedom = 240;
	configure(&c);
	check("Basic Duty Mode now on", rfp_ext_basic_duty());
	uint8_t b[160];
	rfp_config d;
	int n = g_get(b, false);
	check("page reads back what was written", n == RFP_CONF_SER_SIZE && rfp_conf_deserialize(b, &d) && d.reverse_mode == 2 && near(d.rev_ramp, 5.0f, 1e-6));
	rfp_ext_init();                                // "reboot": reload from the emulated EEPROM
	g_get(b, false);
	check("settings survive a restart", rfp_conf_deserialize(b, &d) && d.basic_duty_enable && d.braking_reverse_enable && d.tractor_enable && d.tractor_mode == 1 && near(d.tractor_freedom, 240, 1e-3));
	g_get(b, true);
	check("'default' button gives the defaults", rfp_conf_deserialize(b, &d) && !d.basic_duty_enable && !d.braking_reverse_enable && !d.tractor_enable);
	rfp_conf_set_defaults(&c);
	c.hybrid_band = NAN; c.rev_ramp = -3.0f; c.reverse_mode = 9;
	configure(&c);
	g_get(b, false);
	rfp_conf_deserialize(b, &d);
	check("invalid values are sanitized", d.hybrid_band >= 0.02f && d.hybrid_band <= 0.6f && near(d.rev_ramp, 0.0f, 1e-6) && d.reverse_mode == 0);
	uint8_t bad[160];
	rfp_conf_serialize(bad, &c);
	bad[0] ^= 0xFF;
	check("wrong signature refused", !g_set(bad));

	printf("\n%s (%d failure(s))\n", fails ? "FAILED" : "ALL TESTS PASS", fails);
	return fails ? 1 : 0;
}
