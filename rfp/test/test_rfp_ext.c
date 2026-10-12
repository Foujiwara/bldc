// Host test of the RFP extras logic (hybrid, braking / reverse, tractor, storage).
// Run: bash rfp/test/run_tests.sh
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

// ---- stubs of the firmware services
static mc_configuration m_cfg = { .l_max_duty = 0.95f, .lo_current_max = 60.0f };
static float m_duty, m_rpm;
static int m_tacho;
static float m_brake_rel, m_set_duty, m_set_current;
static int m_brake_calls, m_duty_calls, m_current_calls;

const volatile mc_configuration* mc_interface_get_configuration(void) { return &m_cfg; }
float mc_interface_get_duty_cycle_now(void) { return m_duty; }
float mc_interface_get_rpm(void) { return m_rpm; }
int mc_interface_get_tachometer_value(_Bool reset) { (void)reset; return m_tacho; }
void mc_interface_set_brake_current_rel(float val) { m_brake_rel = val; m_brake_calls++; }
void mc_interface_set_duty(float duty) { m_set_duty = duty; m_duty_calls++; }
void mc_interface_set_current(float current) { m_set_current = current; m_current_calls++; }
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
	uint8_t b[128];
	rfp_conf_serialize(b, c);
	g_set(b);
}

// one relative current request; returns the value passed on (or 999 when consumed)
static float req(float v) {
	float x = v;
	return rfp_ext_current_rel(&x) ? 999.0f : x;
}

int main(void) {
	rfp_config c;

	printf("== Defaults: identical to the official firmware\n");
	rfp_ext_init();
	check("forward request untouched", req(0.7f) == 0.7f);
	m_rpm = 5000; m_duty = 0.4f;
	check("reverse request while rolling: official negative current (Active)", req(-0.6f) == -0.6f);
	m_rpm = 0; m_duty = 0.0f;
	check("reverse request at stop: official negative current (Current, 100 %)", req(-0.6f) == -0.6f);
	m_rpm = -3000; m_duty = -0.3f;
	check("reverse request while reversing: untouched", req(-1.0f) == -1.0f);
	check("Basic Duty Mode off", !rfp_ext_basic_duty());
	uint8_t *xml;
	check("custom page registered, XML available", g_xml && g_xml(&xml) == RFP_CONF_XML_SIZE && xml[4] == 0x78);

	printf("== Hybrid current / duty (forward)\n");
	rfp_conf_set_defaults(&c);
	c.hybrid_enable = 1; c.hybrid_band = 0.2f;
	configure(&c);
	float lim = 0.5f * 0.95f;
	m_rpm = 0; m_duty = 0.0f;
	check("half throttle, standstill: full current", near(req(0.5f), 0.5f, 1e-5));
	m_duty = lim * 0.9f;
	check("half way through the band: half the current", near(req(0.5f), 0.25f, 0.01));
	m_duty = lim;
	check("at the duty limit: zero current", near(req(0.5f), 0.0f, 1e-5));
	m_duty = -0.3f;
	check("rolling backward: full forward current", near(req(0.5f), 0.5f, 1e-5));

	printf("== Braking\n");
	rfp_conf_set_defaults(&c);
	c.braking_mode = 1;
	configure(&c);
	m_rpm = 3000; m_duty = 0.3f; m_brake_calls = 0;
	check("Regen Only: brake current with the request", req(-0.8f) == 999.0f && m_brake_calls == 1 && near(m_brake_rel, 0.8f, 1e-5));
	rfp_conf_set_defaults(&c);
	c.brake_taper_start_erpm = 1500; c.brake_taper_min = 0.3f; c.stop_erpm = 300;
	configure(&c);
	m_rpm = 3000;
	check("Active above the taper: full negative current", near(req(-1.0f), -1.0f, 1e-5));
	m_rpm = 900;      // half way between 300 and 1500
	check("Active in the taper: reduced (0.65 at half way)", near(req(-1.0f), -0.65f, 0.01));
	m_rpm = 310;
	check("Active near Stop ERPM: close to Braking Taper Min", near(req(-1.0f), -0.3f, 0.01));

	printf("== Reverse modes (stopped, Stop ERPM 300)\n");
	rfp_conf_set_defaults(&c);
	c.reverse_mode = 1;
	configure(&c);
	m_rpm = 0; m_duty = 0.0f;
	check("None: zero current at stop", req(-1.0f) == 0.0f);
	m_rpm = 3000;
	check("None: still brakes (Active) while rolling", req(-1.0f) == -1.0f);

	rfp_conf_set_defaults(&c);
	c.reverse_mode = 0; c.rev_cur_max = 0.4f; c.rev_cur_duty_max = 0.2f; c.rev_band = 0.2f;
	configure(&c);
	m_rpm = 0; m_duty = 0.0f;
	check("Current: reverse current capped (40 %)", near(req(-1.0f), -0.4f, 1e-5));
	m_rpm = -2000; m_duty = -0.2f * 0.95f;
	check("Current: zero at the reverse duty limit", near(req(-1.0f), 0.0f, 1e-5));

	rfp_conf_set_defaults(&c);
	c.reverse_mode = 2; c.rev_duty_max = 0.3f;
	configure(&c);
	m_rpm = 0; m_duty = 0.0f; m_duty_calls = 0;
	check("Duty: reverse duty = request x Reverse Duty Max", req(-0.5f) == 999.0f && m_duty_calls == 1 && near(m_set_duty, -0.5f * 0.3f * 0.95f, 1e-5));

	rfp_conf_set_defaults(&c);
	c.reverse_mode = 3; c.smart_cur_max = 0.5f; c.smart_duty_max = 0.1f; c.smart_ramp = 2.0f; c.rev_band = 0.2f;
	configure(&c);
	m_rpm = 0; m_duty = 0.0f;
	req(0.0f);
	float first = 0;
	for (int i = 0;i < 100;i++) { stub_time += 10; first = req(-1.0f); }   // 1 s: cap = half of 0.095
	check("Smart: reverse current capped (50 %) while far from the duty limit", near(first, -0.5f, 1e-4));
	m_duty = -0.0475f;                                                        // at the ramped cap
	stub_time += 10;
	check("Smart: zero current at the ramped duty limit (1 s of a 2 s ramp)", near(req(-1.0f), 0.0f, 0.03));
	for (int i = 0;i < 200;i++) { stub_time += 10; req(-1.0f); }
	m_duty = -0.0475f;
	stub_time += 10;
	check("Smart: after the full ramp, current again at the same duty", req(-1.0f) < -0.3f);

	c.smart_repress = 1;
	configure(&c);
	m_rpm = 3000; m_duty = 0.3f;
	req(-1.0f);                                    // braking
	m_rpm = 0; m_duty = 0.0f;
	check("Re-press: after braking to a stop, no reverse", req(-1.0f) == 0.0f);
	req(0.0f);                                     // neutral
	stub_time += 10;
	check("Re-press: after neutral, reverse allowed", req(-1.0f) < 0.0f);

	printf("== Tractor (spring on the tachometer, 60 deg per step)\n");
	rfp_conf_set_defaults(&c);
	c.tractor_mode = 1; c.tractor_freedom = 120; c.tractor_stiffness = 0.1f; c.tractor_brake = 2.0f;
	c.tractor_max_current = 20; c.tractor_max_erpm = 600; c.tractor_max_lag = 720;
	configure(&c);
	m_rpm = 0; m_tacho = 100; m_current_calls = 0;
	check("throttle request consumed by the tractor", req(0.0f) == 999.0f);
	rfp_ext_tractor_update(0.001f);                // anchor set at the present position
	check("at the anchor: zero current", m_current_calls == 1 && near(m_set_current, 0.0f, 1e-5));
	m_tacho = 102;                                 // pushed forward by 120 deg = inside the free play
	req(0.0f); rfp_ext_tractor_update(0.001f);
	check("inside the free play: zero current", near(m_set_current, 0.0f, 1e-5));
	m_tacho = 105;                                 // 300 deg: 180 beyond the free play
	req(0.0f); rfp_ext_tractor_update(0.001f);
	check("beyond the free play: pulls back (-18 A)", near(m_set_current, -18.0f, 0.01));
	m_tacho = 110;
	req(0.0f); rfp_ext_tractor_update(0.001f);
	check("current limited to Tractor Max Current", near(m_set_current, -20.0f, 1e-4));
	m_tacho = 100; m_rpm = 1000;                   // back at the anchor but moving
	req(0.0f); rfp_ext_tractor_update(0.001f);
	check("brake against the free motion (-2 A at 1000 ERPM)", near(m_set_current, -2.0f, 0.01));
	m_rpm = 0;
	for (int i = 0;i < 500;i++) { req(1.0f); rfp_ext_tractor_update(0.001f); }  // 0.5 s at 600 ERPM: 1800 deg
	check("full throttle: anchor moves ahead, limited by Max Lag (pull forward capped)", m_set_current > 19.9f);
	m_tacho = 100 + 720 / 60; m_rpm = 600;         // the wheel caught up with the anchor (held at Max Lag)
	req(1.0f); rfp_ext_tractor_update(0.001f);
	check("wheel at the anchor, at its speed: no force", fabsf(m_set_current) < 0.5f);
	stub_time += 200;                              // throttle requests stop (app switched to brake)
	m_current_calls = 0;
	rfp_ext_tractor_update(0.001f);
	check("no throttle request for 0.1 s: tractor stops acting", m_current_calls == 0);

	rfp_conf_set_defaults(&c);
	c.tractor_mode = 2; c.tractor_freedom = 60; c.tractor_duty_stiffness = 0.001f; c.tractor_duty_brake = 0.0f;
	c.tractor_max_duty = 0.5f;
	configure(&c);
	m_rpm = 0; m_tacho = 0; m_duty_calls = 0;
	req(0.0f); rfp_ext_tractor_update(0.001f);
	m_tacho = -5;                                  // 300 deg behind: 240 beyond
	req(0.0f); rfp_ext_tractor_update(0.001f);
	check("Spring Duty: duty pulls forward (0.24)", m_duty_calls == 2 && near(m_set_duty, 0.24f, 1e-4));

	printf("== Configuration page and storage\n");
	rfp_conf_set_defaults(&c);
	c.basic_duty_enable = 1; c.reverse_mode = 3; c.braking_mode = 1; c.tractor_mode = 2; c.smart_ramp = 5.0f;
	c.tractor_freedom = 240;
	configure(&c);
	check("Basic Duty Mode now on", rfp_ext_basic_duty());
	uint8_t b[128];
	rfp_config d;
	int n = g_get(b, false);
	check("page reads back what was written", n == RFP_CONF_SER_SIZE && rfp_conf_deserialize(b, &d) && d.reverse_mode == 3 && near(d.smart_ramp, 5.0f, 1e-6));
	rfp_ext_init();                                // "reboot": reload from the emulated EEPROM
	g_get(b, false);
	check("settings survive a restart", rfp_conf_deserialize(b, &d) && d.basic_duty_enable && d.braking_mode == 1 && d.tractor_mode == 2 && near(d.tractor_freedom, 240, 1e-3));
	g_get(b, true);
	check("'default' button gives the defaults", rfp_conf_deserialize(b, &d) && !d.basic_duty_enable && d.reverse_mode == 0 && d.tractor_mode == 0);
	rfp_conf_set_defaults(&c);
	c.hybrid_band = NAN; c.smart_ramp = -3.0f; c.reverse_mode = 9;
	configure(&c);
	g_get(b, false);
	rfp_conf_deserialize(b, &d);
	check("invalid values are sanitized", d.hybrid_band >= 0.02f && d.hybrid_band <= 0.6f && near(d.smart_ramp, 0.01f, 1e-6) && d.reverse_mode == 0);
	uint8_t bad[128];
	rfp_conf_serialize(bad, &c);
	bad[0] ^= 0xFF;
	check("wrong signature refused", !g_set(bad));

	printf("\n%s (%d failure(s))\n", fails ? "FAILED" : "ALL TESTS PASS", fails);
	return fails ? 1 : 0;
}
