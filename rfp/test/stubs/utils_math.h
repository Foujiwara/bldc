#ifndef STUB_UTILS_MATH_H_
#define STUB_UTILS_MATH_H_
static inline void utils_truncate_number(float *n, float lo, float hi) { if (*n > hi) *n = hi; else if (*n < lo) *n = lo; }
static inline void utils_step_towards(float *v, float goal, float step) {
	if (*v < goal) { if (*v + step < goal) *v += step; else *v = goal; }
	else if (*v > goal) { if (*v - step > goal) *v -= step; else *v = goal; }
}
#endif
