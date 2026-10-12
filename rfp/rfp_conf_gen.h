// GENERE par rfp/gen_rfp_conf.py - ne pas editer.
#ifndef RFP_CONF_GEN_H_
#define RFP_CONF_GEN_H_

#include <stdint.h>
#include <stdbool.h>

// braking_mode : 0 = Active (negative current), 1 = Regen Only (brake)
// reverse_mode : 0 = Current, 1 = None, 2 = Duty, 3 = Smart
// tractor_mode : 0 = Spring Current, 1 = Spring Duty
// tractor_speed_mode : 0 = Throttle, 1 = Preset

typedef struct {
	bool basic_duty_enable;
	bool hybrid_enable;
	float hybrid_duty_limit;
	float hybrid_band;
	bool braking_reverse_enable;
	uint8_t braking_mode;
	uint8_t reverse_mode;
	float stop_erpm;
	float stop_hyst;
	float rev_transition;
	float brake_taper_start_erpm;
	float brake_taper_min;
	float rev_cur_max;
	float rev_cur_duty_max;
	float rev_band;
	float rev_duty_max;
	float smart_cur_max;
	float smart_duty_max;
	float smart_ramp;
	bool smart_repress;
	bool tractor_enable;
	uint8_t tractor_mode;
	uint8_t tractor_speed_mode;
	float tractor_max_erpm;
	float tractor_freedom;
	float tractor_stiffness;
	float tractor_brake;
	float tractor_max_current;
	float tractor_max_lag;
	float tractor_duty_stiffness;
	float tractor_duty_brake;
	float tractor_max_duty;
} rfp_config;

#define RFP_CONFIG_SIGNATURE	2680818977u
#define RFP_CONF_SER_SIZE	105
#define RFP_CONF_XML_SIZE	4413

int32_t rfp_conf_serialize(uint8_t *buffer, const rfp_config *conf);
bool rfp_conf_deserialize(const uint8_t *buffer, rfp_config *conf);
void rfp_conf_set_defaults(rfp_config *conf);
extern const uint8_t rfp_conf_xml_data[];

#endif
