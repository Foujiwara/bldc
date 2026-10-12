// GENERE par rfp/gen_rfp_conf.py - ne pas editer.
#ifndef RFP_CONF_GEN_H_
#define RFP_CONF_GEN_H_

#include <stdint.h>
#include <stdbool.h>

// braking_mode : 0 = Active (negative current), 1 = Regen Only (brake)
// reverse_mode : 0 = Current, 1 = None, 2 = Duty, 3 = Current, Duty Limited
// tractor_mode : 0 = Spring Current, 1 = Spring Duty
// tractor_speed_mode : 0 = Throttle, 1 = Preset
// adc_ctrl_type : 0 = App ADC Setting, 1 = Current Reverse ADC2

typedef struct {
	bool basic_duty_enable;
	bool hybrid_enable;
	float hybrid_duty_limit;
	float hybrid_band;
	bool braking_reverse_enable;
	float stop_erpm;
	float stop_hyst;
	float rev_transition;
	uint8_t braking_mode;
	float brake_taper_start_erpm;
	float brake_taper_min;
	uint8_t reverse_mode;
	float rev_cur_max;
	float rev_duty_max;
	float rev_band;
	float rev_ramp;
	bool rev_repress;
	float rev_arm_level;
	float rev_disarm_time;
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
	uint8_t adc_ctrl_type;
	bool adc_brake_priority;
} rfp_config;

#define RFP_CONFIG_SIGNATURE	3196129770u
#define RFP_CONF_SER_SIZE	103
#define RFP_CONF_XML_SIZE	5145

int32_t rfp_conf_serialize(uint8_t *buffer, const rfp_config *conf);
bool rfp_conf_deserialize(const uint8_t *buffer, rfp_config *conf);
void rfp_conf_set_defaults(rfp_config *conf);
extern const uint8_t rfp_conf_xml_data[];

#endif
