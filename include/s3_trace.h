#ifndef S3_TRACE_H
#define S3_TRACE_H

#include "s3_abi.h"

/* Bring-up trace: with -DS3_TRACE, each DDA entry appends one line to
   C:\S3TRACE.TXT in the guest through KERNEL file calls only (display
   drivers load before USER and GDI). Host tests and normal builds compile
   the calls out. */
#if defined(S3_TRACE) && !defined(S3_HOST_TEST)
void S3_Trace(const char S3_FAR *event, S3_U16 a, S3_U16 b);
#define S3_TRACE_EVENT(event, a, b) S3_Trace((event), (S3_U16)(a), (S3_U16)(b))
#else
#define S3_TRACE_EVENT(event, a, b) ((void)0)
#endif

#endif
