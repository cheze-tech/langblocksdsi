/* -------------------------------------------------------------------------
 * platform.h -- the one place that knows whether we are building for the DSi
 *               or for the host PC (used by tools/test_tetris_host.c).
 *
 * On the DSi we simply pull in <nds.h>, which gives us u8/u16/u32 and the
 * whole libnds API.  When HOST_TEST is defined we provide the same small set
 * of integer typedefs with <stdint.h> so that pure-logic modules (tetris.c)
 * can be compiled and fuzz-tested with a normal desktop compiler.
 * ------------------------------------------------------------------------- */
#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef HOST_TEST
#include <stdint.h>
#include <stddef.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
#else
#include <nds.h>
#endif

#endif /* PLATFORM_H */
