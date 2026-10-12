// GENERE par rfp/gen_rfp_conf.py - ne pas editer.
#ifndef RFP_CONF_GEN_H_
#define RFP_CONF_GEN_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	bool basic_duty_enable;
	bool hybrid_enable;
	float hybrid_duty_limit;
	float hybrid_band;
	bool smart_rev_enable;
	float smart_rev_erpm;
	float smart_rev_max_duty;
	float smart_rev_ramp;
} rfp_config;

#define RFP_CONFIG_SIGNATURE	3742035091u
#define RFP_CONF_XML_SIZE	2103

int32_t rfp_conf_serialize(uint8_t *buffer, const rfp_config *conf);
bool rfp_conf_deserialize(const uint8_t *buffer, rfp_config *conf);
void rfp_conf_set_defaults(rfp_config *conf);
extern const uint8_t rfp_conf_xml_data[];

#endif
