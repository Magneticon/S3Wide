#ifndef S3_COPY_H
#define S3_COPY_H

#include "s3_abi.h"

/* Far memory copy and fill for the drawing paths. The driver needs a 386
   (Enhanced Mode, DPMI linear frame buffers), so the copy moves dwords;
   the C library's _fmemcpy moves words. Buffers must not overlap. */
#if defined(S3_HOST_TEST)
#include <string.h>
#define S3_COPY(d, s, n) memcpy((d), (s), (n))
#define S3_FILL(d, v, n) memset((d), (v), (n))
#else
#include <string.h>
void s3_rep_movsd(void S3_FAR *destination, const void S3_FAR *source,
                  S3_U16 bytes);
/* DS:SI -> ES:DI; rep movsd (F3 66 A5) for bytes / 4, then rep movsb. */
#pragma aux s3_rep_movsd = "push ds" "mov ds, dx" "mov dx, cx" "shr cx, 1" "shr cx, 1" 0xF3 0x66 0xA5 "mov cx, dx" "and cx, 3" "rep movsb" "pop ds" parm [es di] [dx si] [cx] modify exact [cx dx si di];
#define S3_COPY(d, s, n) s3_rep_movsd((d), (s), (n))
#define S3_FILL(d, v, n) _fmemset((d), (v), (n))
#endif

/* d = d op x over dwords dwords (x unused for NOT). */
#define S3_ROWOP_AND   1U  /* d & x */
#define S3_ROWOP_OR    2U  /* d | x */
#define S3_ROWOP_XOR   3U  /* d ^ x */
#define S3_ROWOP_NOT   4U  /* ~d */
#define S3_ROWOP_ANDN  5U  /* d & ~x */
#define S3_ROWOP_ORN   6U  /* d | ~x */
#define S3_ROWOP_COPYN 7U  /* ~x */
void S3_RowOp(S3_U16 op, S3_U32 S3_FAR *d, const S3_U32 S3_FAR *x, S3_U16 dwords);

#endif
