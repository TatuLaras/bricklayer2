#ifndef _MATH_MACROS
#define _MATH_MACROS

#define MIN(x, y) ((x < y) ? x : y)
#define MAX(x, y) ((x > y) ? x : y)
#define CLAMP(x, max, min) (MAX(MIN(x, max), min))
#define COUNT(arr) (sizeof arr / sizeof *arr)

#endif
