#ifndef STUB_CONF_GENERAL_H_
#define STUB_CONF_GENERAL_H_
#include <stdint.h>
#include <stdbool.h>
typedef union { uint32_t as_u32; int32_t as_i32; float as_float; } eeprom_var;
bool conf_general_read_eeprom_var_custom(eeprom_var *v, int address);
bool conf_general_store_eeprom_var_custom(eeprom_var *v, int address);
#endif
