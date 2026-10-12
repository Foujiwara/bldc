#ifndef STUB_CH_H_
#define STUB_CH_H_
#include <stdint.h>
typedef uint32_t systime_t;
typedef struct { int dummy; } thread_t;
extern systime_t stub_time;
extern thread_t *stub_self;
static inline systime_t chVTGetSystemTimeX(void) { return stub_time; }
static inline thread_t *chThdGetSelfX(void) { return stub_self; }
#define THD_WORKING_AREA(name, size) uint8_t name[size]
#define THD_FUNCTION(name, arg) void name(void *arg)
#define NORMALPRIO 128
static inline void chRegSetThreadName(const char *n) { (void)n; }
static inline void chThdSleepMilliseconds(int ms) { (void)ms; }
extern thread_t stub_rfp_thread;
static inline thread_t *chThdCreateStatic(void *wa, unsigned size, int prio, void (*fn)(void*), void *arg) {
	(void)wa; (void)size; (void)prio; (void)fn; (void)arg; return &stub_rfp_thread; }
#endif
