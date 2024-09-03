#ifndef BASICS_H
#define BASICS_H

#include "ulib/platformutil.h"
#include "ulib/stringutil.h"
#include "ulib/fileutil.h"


#define SCRIBBLE_LOG PLATFORM_LOG

typedef double Dim;
typedef int64_t Timestamp;

#define MIN std::min
#define MAX std::max
#define ABS std::abs
#define SGN(x) ((x) >= 0 ? 1 : -1)
//#define CLAMP(x, min, max) std::min(MAX(x, min), max)
// number of elements in an array
#define NELEM(a) (sizeof(a)/sizeof(a[0]))
#define NELEMI(a) ((int)(sizeof(a)/sizeof(a[0])))

#define MAX_DIM REAL_MAX
#define MIN_DIM REAL_MIN
// previously, we had MIN/MAX_X/Y_DIM to provide better annotation, but they weren't used consistently

#ifndef NDEBUG
#define SCRIBBLE_TEST 1
#endif

extern bool SCRIBBLE_DEBUG;

#endif
