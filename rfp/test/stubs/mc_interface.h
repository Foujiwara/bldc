#ifndef STUB_MC_INTERFACE_H_
#define STUB_MC_INTERFACE_H_
typedef struct { float l_max_duty; } mc_configuration;
const volatile mc_configuration* mc_interface_get_configuration(void);
float mc_interface_get_duty_cycle_now(void);
float mc_interface_get_rpm(void);
void mc_interface_set_brake_current_rel(float val);
void mc_interface_set_duty(float duty);
#endif
