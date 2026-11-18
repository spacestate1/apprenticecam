/* ghidra.h - the type names and pseudo-operators Ghidra's decompiler prints, as C, for the TrueShape engine port
 * (viewer/nest/ts/*.c are transliterations of decomp/nest/n64dll_all.c, see ../nest_ts.h).  Windows x64 data model
 * (LLP64): Ghidra's "long" / "ulong" are 4 bytes. */
#ifndef TS_GHIDRA_H
#define TS_GHIDRA_H
#include <stdint.h>
#include <string.h>
#include <math.h>

typedef unsigned char undefined; typedef unsigned char undefined1; typedef uint16_t undefined2;
typedef uint32_t undefined3; typedef uint32_t undefined4; typedef uint64_t undefined5; typedef uint64_t undefined6;
typedef uint64_t undefined7; typedef uint64_t undefined8;
typedef unsigned char byte; typedef unsigned char uchar; typedef signed char sbyte; typedef uint16_t ushort; typedef uint16_t word;
typedef uint32_t uint; typedef uint32_t dword; typedef uint64_t qword;
typedef int32_t ts_long; typedef uint32_t ts_ulong;   /* "long" is rewritten to ts_long by the generator */
typedef int64_t longlong; typedef uint64_t ulonglong;
typedef unsigned char bool_t;
typedef uint16_t wchar16;
typedef unsigned __int128 undefined16;
typedef int32_t ts_clock_t;
typedef long double float10;
typedef uint64_t code();          /* unprototyped: called through casts with any arguments */
typedef void *pointer; typedef void *pointer64; typedef uint64_t ImageBaseOffset32;
typedef unsigned int uint3;

#define CONCAT11(a,b) ((uint16_t)(((uint16_t)(uint8_t)(a) << 8) | (uint8_t)(b)))
#define CONCAT12(a,b) ((uint32_t)(((uint32_t)(uint8_t)(a) << 16) | (uint16_t)(b)))
#define CONCAT13(a,b) ((uint32_t)(((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(b) & 0xffffff)))
#define CONCAT22(a,b) ((uint32_t)(((uint32_t)(uint16_t)(a) << 16) | (uint16_t)(b)))
#define CONCAT31(a,b) ((uint32_t)(((uint32_t)(a) << 8) | (uint8_t)(b)))
#define CONCAT44(a,b) ((uint64_t)(((uint64_t)(uint32_t)(a) << 32) | (uint32_t)(b)))
#define CONCAT71(a,b) ((uint64_t)(((uint64_t)(a) << 8) | (uint8_t)(b)))
#define CONCAT62(a,b) ((uint64_t)(((uint64_t)(a) << 16) | (uint16_t)(b)))
#define CONCAT53(a,b) ((uint64_t)(((uint64_t)(a) << 24) | ((uint64_t)(b) & 0xffffff)))
#define CONCAT35(a,b) ((uint64_t)(((uint64_t)(a) << 40) | ((uint64_t)(b) & 0xffffffffffULL)))
#define CONCAT26(a,b) ((uint64_t)(((uint64_t)(a) << 48) | ((uint64_t)(b) & 0xffffffffffffULL)))
#define CONCAT17(a,b) ((uint64_t)(((uint64_t)(a) << 56) | ((uint64_t)(b) & 0xffffffffffffffULL)))
static inline uint64_t ts_sub_bits(uint64_t v, int off) { return off >= 8 ? 0 : v >> (off * 8); }
#define SUB81(v,o) ((uint8_t)ts_sub_bits((uint64_t)(v),(o)))
#define SUB82(v,o) ((uint16_t)ts_sub_bits((uint64_t)(v),(o)))
#define SUB84(v,o) ((uint32_t)ts_sub_bits((uint64_t)(v),(o)))
#define SUB41(v,o) ((uint8_t)((uint32_t)(v) >> ((o)*8)))
#define SUB42(v,o) ((uint16_t)((uint32_t)(v) >> ((o)*8)))
#define SUB21(v,o) ((uint8_t)((uint16_t)(v) >> ((o)*8)))
#define ZEXT14(v) ((uint32_t)(uint8_t)(v))
#define ZEXT18(v) ((uint64_t)(uint8_t)(v))
#define ZEXT24(v) ((uint32_t)(uint16_t)(v))
#define ZEXT28(v) ((uint64_t)(uint16_t)(v))
#define ZEXT48(v) ((uint64_t)(uint32_t)(v))
#define SEXT14(v) ((int32_t)(int8_t)(v))
#define SEXT18(v) ((int64_t)(int8_t)(v))
#define SEXT24(v) ((int32_t)(int16_t)(v))
#define SEXT28(v) ((int64_t)(int16_t)(v))
#define SEXT48(v) ((int64_t)(int32_t)(v))
#define CARRY1(a,b) ((uint8_t)((uint8_t)(a) + (uint8_t)(b)) < (uint8_t)(a))
#define CARRY4(a,b) ((uint32_t)((uint32_t)(a) + (uint32_t)(b)) < (uint32_t)(a))
#define CARRY8(a,b) ((uint64_t)((uint64_t)(a) + (uint64_t)(b)) < (uint64_t)(a))
#define SCARRY4(a,b) __builtin_add_overflow_p((int32_t)(a), (int32_t)(b), (int32_t)0)
#define SCARRY8(a,b) __builtin_add_overflow_p((int64_t)(a), (int64_t)(b), (int64_t)0)
#define SBORROW4(a,b) __builtin_sub_overflow_p((int32_t)(a), (int32_t)(b), (int32_t)0)
#define SBORROW8(a,b) __builtin_sub_overflow_p((int64_t)(a), (int64_t)(b), (int64_t)0)
#define SBORROW1(a,b) __builtin_sub_overflow_p((int8_t)(a), (int8_t)(b), (int8_t)0)
#define ZEXT816(v) ((unsigned __int128)(uint64_t)(v))
#define ZEXT216(v) ((unsigned __int128)(uint16_t)(v))
#define ZEXT416(v) ((unsigned __int128)(uint32_t)(v))
#define SUB168(v,o) ((uint64_t)((unsigned __int128)(v) >> ((o)*8)))
#define POPCOUNT(v) __builtin_popcountll((uint64_t)(v))
static inline uint64_t ts_dbits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static inline double ts_bd(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static inline float ts_bf(uint32_t u) { float d; memcpy(&d, &u, 4); return d; }
#define ts_db(d) ts_dbits(d)
#define NAN(x) isnan(x)
#define ABS(x) fabs(x)
#define SQRT(x) sqrt(x)
#endif
