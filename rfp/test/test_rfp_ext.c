// Host test of the RFP extras logic (hybrid current / duty, smart reverse, storage).
// Run: rfp/test/run_tests.sh
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
static mc_configuration m_cfg = { .l_max_duty = 0.95f };
static float m_duty, m_rpm;
static float m_brake_rel, m_set_duty;
static int m_brake_calls, m_duty_calls;

const volatile mc_configuration* mc_interface_get_configuration(void) { return &m_cfg; }
float mc_interface_get_duty_cycle_now(void) { return m_duty; }
float mc_interface_get_rpm(void) { return m_rpm; }
void mc_interface_set_brake_current_rel(float val) { m_brake_rel = val; m_brake_calls++; }
void mc_interface_set_duty(float duty) { m_set_duty = duty; m_duty_calls++; }

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
	uint8_t b[64];
	rfp_conf_serialize(b, c);
	g_set(b);
}

int main(void) {
	rfp_config c;

	printf("== Default: everything off, the firmware behaves like the official one\n");
	rfp_ext_init();
	float v = 0.7f;
	check("no extra function: request untouched and not consumed", !rfp_ext_current_rel(&v) && v == 0.7f);
	check("Basic Duty Mode off", !rfp_ext_basic_duty());
	uint8_t *xml;
	check("custom page registered, XML available", g_xml && g_xml(&xml) == RFP_CONF_XML_SIZE && xml[4] == 0x78);

	printf("== Hybrid current / duty (max duty 0.95, limit 1.0, band 0.2)\n");
	rfp_conf_set_defaults(&c);
	c.hybrid_enable = 1; c.hybrid_duty_limit = 1.0f; c.hybrid_band = 0.2f;
	configure(&c);
	float lim = 0.5f * 0.95f;               // limit at half throttle
	m_duty = 0.0f; v = 0.5f;
	check("half throttle, standstill: full current", !rfp_ext_current_rel(&v) && near(v, 0.5f, 1e-5));
	m_duty = lim * 0.9f; v = 0.5f;          // halfway through the band
	rfp_ext_current_rel(&v);
	check("half way through the band: half the current", near(v, 0.25f, 0.01));
	m_duty = lim; v = 0.5f;
	rfp_ext_current_rel(&v);
	check("at the duty limit: zero current", near(v, 0.0f, 1e-5));
	m_duty = lim + 0.1f; v = 0.5f;
	rfp_ext_current_rel(&v);
	check("above the limit: zero current (never negative)", near(v, 0.0f, 1e-5));
	m_duty = -0.3f; v = 0.5f;
	rfp_ext_current_rel(&v);
	check("rolling the other way: full current", near(v, 0.5f, 1e-5));
	m_duty = 0.2f; v = 1.0f;
	rfp_ext_current_rel(&v);
	check("full throttle at 0.2 duty: full current", near(v, 1.0f, 1e-5));
	m_duty = -lim; v = -0.5f;
	rfp_ext_current_rel(&v);
	check("reverse request: same law in the other direction", near(v, 0.0f, 1e-5));
	m_duty = 0.9f; v = 0.0f;
	check("zero request: untouched", !rfp_ext_current_rel(&v) && v == 0.0f);

	printf("== Smart reverse (brake above 1000 ERPM, 0.07 duty over 3 s)\n");
	rfp_conf_set_defaults(&c);
	c.smart_rev_enable = 1; c.smart_rev_erpm = 1000; c.smart_rev_max_duty = 0.07f; c.smart_rev_ramp = 3.0f;
	configure(&c);
	m_rpm = 3000; m_duty = 0.3f; v = -0.8f; m_brake_calls = 0;
	check("reverse request while rolling forward: brake with the request", rfp_ext_current_rel(&v) && m_brake_calls == 1 && near(m_brake_rel, 0.8f, 1e-5));
	m_rpm = 500; m_duty = 0.0f; m_duty_calls = 0;
	v = 0.0f; rfp_ext_current_rel(&v);       // the apps call every cycle: the ramp follows the real duty
	for (int i = 0;i < 150;i++) {            // 1.5 s at 100 Hz
		stub_time += 10; v = -1.0f;
		rfp_ext_current_rel(&v);
	}
	check("slow: reverse by duty, ramped (-0.035 after half the ramp)", m_duty_calls == 150 && near(m_set_duty, -0.035f, 0.003));
	for (int i = 0;i < 400;i++) { stub_time += 10; v = -1.0f; rfp_ext_current_rel(&v); }
	check("capped at the maximum reverse duty", near(m_set_duty, -0.07f, 1e-4));
	for (int i = 0;i < 20;i++) { stub_time += 10; v = -0.5f; rfp_ext_current_rel(&v); }
	check("half request: reverse duty comes back toward half the maximum", m_set_duty > -0.07f);
	v = 0.4f;
	check("forward request: normal current path", !rfp_ext_current_rel(&v) && v == 0.4f);
	m_rpm = 3000; m_duty = 0.0f; m_brake_calls = 0; v = -0.0005f;
	check("tiny negative request is not a reverse request", !rfp_ext_current_rel(&v));

	printf("== Configuration page and storage\n");
	rfp_conf_set_defaults(&c);
	c.basic_duty_enable = 1; c.hybrid_enable = 1; c.hybrid_duty_limit = 0.8f; c.hybrid_band = 0.3f;
	c.smart_rev_enable = 1; c.smart_rev_erpm = 1500; c.smart_rev_max_duty = 0.1f; c.smart_rev_ramp = 5.0f;
	configure(&c);
	check("Basic Duty Mode now on", rfp_ext_basic_duty());
	uint8_t b[64];
	int n = g_get(b, false);
	rfp_config d;
	check("page reads back what was written", n == 27 && rfp_conf_deserialize(b, &d) && d.hybrid_enable && near(d.hybrid_duty_limit, 0.8f, 1e-6) && near(d.smart_rev_ramp, 5.0f, 1e-6));
	rfp_ext_init();                          // "reboot": reload from the emulated EEPROM
	n = g_get(b, false);
	check("settings survive a restart", rfp_conf_deserialize(b, &d) && d.basic_duty_enable && d.smart_rev_enable && near(d.smart_rev_erpm, 1500, 1e-3) && near(d.hybrid_band, 0.3f, 1e-6));
	n = g_get(b, true);
	check("'default' button gives the defaults", rfp_conf_deserialize(b, &d) && !d.basic_duty_enable && !d.hybrid_enable && near(d.hybrid_band, 0.15f, 1e-6));
	rfp_conf_set_defaults(&c);
	c.hybrid_band = NAN; c.smart_rev_ramp = -3.0f; c.hybrid_duty_limit = 9.0f;
	configure(&c);
	n = g_get(b, false);
	rfp_conf_deserialize(b, &d);
	check("invalid values are sanitized", d.hybrid_band >= 0.02f && d.hybrid_band <= 0.6f && near(d.smart_rev_ramp, 0.01f, 1e-6) && near(d.hybrid_duty_limit, 1.0f, 1e-6));
	uint8_t bad[64];
	rfp_conf_serialize(bad, &c);
	bad[0] ^= 0xFF;
	check("wrong signature refused", !g_set(bad));

	printf("\n%s (%d failure(s))\n", fails ? "FAILED" : "ALL TESTS PASS", fails);
	return fails ? 1 : 0;
}
