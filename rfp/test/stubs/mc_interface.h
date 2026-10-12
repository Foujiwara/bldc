#ifndef STUB_MC_INTERFACE_H_
#define STUB_MC_INTERFACE_H_
typedef struct { float l_max_duty; float lo_current_max; float lo_current_min; float l_abs_current_max; float cc_min_current; } mc_configuration;
const volatile mc_configuration* mc_interface_get_configuration(void);
float mc_interface_get_duty_cycle_now(void);
float mc_interface_get_rpm(void);
int mc_interface_get_tachometer_value(_Bool reset);
void mc_interface_set_brake_current_rel(float val);
void mc_interface_set_duty(float duty);
void mc_interface_set_current(float current);
void mc_interface_set_current_off_delay(float delay_sec);
#endif
