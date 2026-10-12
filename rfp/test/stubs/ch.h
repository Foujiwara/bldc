#ifndef STUB_CH_H_
#define STUB_CH_H_
#include <stdint.h>
typedef uint32_t systime_t;
extern systime_t stub_time;
static inline systime_t chVTGetSystemTimeX(void) { return stub_time; }
#define THD_WORKING_AREA(name, size) uint8_t name[size]
#define THD_FUNCTION(name, arg) void name(void *arg)
#define NORMALPRIO 128
static inline void chRegSetThreadName(const char *n) { (void)n; }
static inline void chThdSleepMilliseconds(int ms) { (void)ms; }
static inline void *chThdCreateStatic(void *wa, unsigned size, int prio, void (*fn)(void*), void *arg) {
	(void)wa; (void)size; (void)prio; (void)fn; (void)arg; return 0; }
#endif
