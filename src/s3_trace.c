#include "s3_trace.h"

#if defined(S3_TRACE) && !defined(S3_HOST_TEST)

#define S3_TRACE_PATH "C:\\S3TRACE.TXT"
#define S3_HFILE_ERROR (-1)
#define S3_OF_WRITE 1
#define S3_SEEK_END 2

/* KERNEL file API, resolved through Open Watcom's windows.lib. */
extern int S3_FAR S3_PASCAL _lopen(const char S3_FAR *path, int mode);
extern int S3_FAR S3_PASCAL _lcreat(const char S3_FAR *path, int attribute);
extern long S3_FAR S3_PASCAL _llseek(int file, long offset, int origin);
extern unsigned S3_FAR S3_PASCAL _lwrite(int file, const char S3_FAR *buffer,
                                         unsigned count);
extern int S3_FAR S3_PASCAL _lclose(int file);

static char *s3_put_hex(char *out, S3_U16 value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;

    for (shift = 12; shift >= 0; shift -= 4) {
        *out++ = digits[(value >> shift) & 0x0F];
    }
    return out;
}

void S3_Trace(const char S3_FAR *event, S3_U16 a, S3_U16 b)
{
    char line[64];
    char *out = line;
    int file;

    while (*event != '\0' && out < line + 40) {
        *out++ = *event++;
    }
    *out++ = ' ';
    out = s3_put_hex(out, a);
    *out++ = ' ';
    out = s3_put_hex(out, b);
    *out++ = '\r';
    *out++ = '\n';

    /* Reopen per line so the log survives a guest hang or crash. */
    file = _lopen(S3_TRACE_PATH, S3_OF_WRITE);
    if (file == S3_HFILE_ERROR) {
        file = _lcreat(S3_TRACE_PATH, 0);
        if (file == S3_HFILE_ERROR) {
            return;
        }
    }
    _llseek(file, 0L, S3_SEEK_END);
    _lwrite(file, line, (unsigned)(out - line));
    _lclose(file);
}

#else
/* ISO C requires at least one declaration per translation unit. */
typedef int s3_trace_disabled;
#endif
