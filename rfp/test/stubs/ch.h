#ifndef STUB_CH_H_
#define STUB_CH_H_
#include <stdint.h>
typedef uint32_t systime_t;
extern systime_t stub_time;
static inline systime_t chVTGetSystemTimeX(void) { return stub_time; }
#endif
