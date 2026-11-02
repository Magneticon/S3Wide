#include "s3_copy.h"

/* Row operations on whole dwords. The Win16 build uses 386 string and
   32-bit instructions directly: Watcom's 16-bit code generator splits every
   32-bit operation into word halves. Each block saves EAX, whose upper half
   compiled code does not expect to change. */

#if defined(S3_HOST_TEST)
void S3_RowOp(S3_U16 op, S3_U32 S3_FAR *d, const S3_U32 S3_FAR *x, S3_U16 dwords)
{
    S3_U16 i;
    for (i = 0; i < dwords; ++i) {
        switch (op) {
        case S3_ROWOP_AND:   d[i] &= x[i]; break;
        case S3_ROWOP_OR:    d[i] |= x[i]; break;
        case S3_ROWOP_XOR:   d[i] ^= x[i]; break;
        case S3_ROWOP_NOT:   d[i] = ~d[i]; break;
        case S3_ROWOP_ANDN:  d[i] &= ~x[i]; break;
        case S3_ROWOP_ORN:   d[i] |= ~x[i]; break;
        case S3_ROWOP_COPYN: d[i] = ~x[i]; break;
        default: break;
        }
    }
}
#else
void S3_RowOp(S3_U16 op, S3_U32 S3_FAR *d, const S3_U32 S3_FAR *x, S3_U16 dwords)
{
    if (dwords == 0) return;
    if (op == S3_ROWOP_NOT) x = d; /* unused; keep DS:SI valid */
    switch (op) {
    case S3_ROWOP_AND:
        __asm {
            push ds
            push eax
            les di, d
            lds si, x
            mov cx, dwords
        and_next:
            lodsd
            and es:[di], eax
            add di, 4
            loop and_next
            pop eax
            pop ds
        }
        break;
    case S3_ROWOP_OR:
        __asm {
            push ds
            push eax
            les di, d
            lds si, x
            mov cx, dwords
        or_next:
            lodsd
            or es:[di], eax
            add di, 4
            loop or_next
            pop eax
            pop ds
        }
        break;
    case S3_ROWOP_XOR:
        __asm {
            push ds
            push eax
            les di, d
            lds si, x
            mov cx, dwords
        xor_next:
            lodsd
            xor es:[di], eax
            add di, 4
            loop xor_next
            pop eax
            pop ds
        }
        break;
    case S3_ROWOP_NOT:
        __asm {
            les di, d
            mov cx, dwords
        not_next:
            not dword ptr es:[di]
            add di, 4
            loop not_next
        }
        break;
    case S3_ROWOP_ANDN:
        __asm {
            push ds
            push eax
            les di, d
            lds si, x
            mov cx, dwords
        andn_next:
            lodsd
            not eax
            and es:[di], eax
            add di, 4
            loop andn_next
            pop eax
            pop ds
        }
        break;
    case S3_ROWOP_ORN:
        __asm {
            push ds
            push eax
            les di, d
            lds si, x
            mov cx, dwords
        orn_next:
            lodsd
            not eax
            or es:[di], eax
            add di, 4
            loop orn_next
            pop eax
            pop ds
        }
        break;
    case S3_ROWOP_COPYN:
        __asm {
            push ds
            push eax
            les di, d
            lds si, x
            mov cx, dwords
        copyn_next:
            lodsd
            not eax
            stosd
            loop copyn_next
            pop eax
            pop ds
        }
        break;
    default:
        break;
    }
}
#endif
