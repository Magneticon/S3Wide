#include "s3_win16_native.h"

#define NOGDI
#define NODRIVERS
#include <win/win16.h>
#include <i86.h>

static int S3_FAR S3_PASCAL s3_native_current_code_selector(
    void S3_FAR *context, S3_U16 S3_FAR *selector)
{
    S3_U16 current;
    (void)context;
    if (selector == (S3_U16 S3_FAR *)0) return 0;
    __asm {
        mov ax, cs
        mov current, ax
    }
    *selector = current;
    return current != 0;
}

static int S3_FAR S3_PASCAL s3_native_alloc_selector(
    void S3_FAR *context, S3_U16 source,
    S3_U16 S3_FAR *selector, S3_U32 S3_FAR *original_limit)
{
    S3_U32 source_limit;
    S3_U32 allocated_limit;
    UINT allocated;
    (void)context;
    if (selector == (S3_U16 S3_FAR *)0 ||
        original_limit == (S3_U32 S3_FAR *)0) return 0;

    /* AllocSelector tiles sources larger than 64 KiB. The bridge supports
       exactly one aperture selector, so reject those before allocation. */
    source_limit = GetSelectorLimit((UINT)source);
    if (source_limit > 0xFFFFUL) return 0;
    allocated = AllocSelector((UINT)source);
    if (allocated == 0) return 0;

    allocated_limit = GetSelectorLimit(allocated);
    *selector = (S3_U16)allocated;
    *original_limit = allocated_limit;
    return 1;
}

static int S3_FAR S3_PASCAL s3_native_make_data_selector(
    void S3_FAR *context, S3_U16 source, S3_U16 selector)
{
    (void)context;
    return PrestoChangoSelector((UINT)source, (UINT)selector) != 0;
}

static int S3_FAR S3_PASCAL s3_native_set_selector_base(
    void S3_FAR *context, S3_U16 selector, S3_U32 base)
{
    (void)context;
    (void)SetSelectorBase((UINT)selector, (DWORD)base);
    return GetSelectorBase((UINT)selector) == (DWORD)base;
}

static int S3_FAR S3_PASCAL s3_native_set_selector_limit(
    void S3_FAR *context, S3_U16 selector, S3_U32 limit)
{
    (void)context;
    (void)SetSelectorLimit((UINT)selector, (DWORD)limit);
    return GetSelectorLimit((UINT)selector) == (DWORD)limit;
}

static int S3_FAR S3_PASCAL s3_native_get_selector_base(
    void S3_FAR *context, S3_U16 selector, S3_U32 S3_FAR *base)
{
    (void)context;
    if (base == (S3_U32 S3_FAR *)0) return 0;
    *base = (S3_U32)GetSelectorBase((UINT)selector);
    return 1;
}

static int S3_FAR S3_PASCAL s3_native_get_selector_limit(
    void S3_FAR *context, S3_U16 selector, S3_U32 S3_FAR *limit)
{
    (void)context;
    if (limit == (S3_U32 S3_FAR *)0) return 0;
    *limit = (S3_U32)GetSelectorLimit((UINT)selector);
    return 1;
}

static int S3_FAR S3_PASCAL s3_native_selector_window(
    void S3_FAR *context, S3_U16 selector,
    void S3_FAR * S3_FAR *window)
{
    (void)context;
    if (window == (void S3_FAR * S3_FAR *)0) return 0;
    *window = (void S3_FAR *)MK_FP((UINT)selector, 0);
    return *window != (void S3_FAR *)0;
}

static int S3_FAR S3_PASCAL s3_native_free_selector(
    void S3_FAR *context, S3_U16 selector, S3_U32 original_limit)
{
    (void)context;
    /* Q132005 requires FreeSelector's limit to match the limit cloned by
       AllocSelector. Restore and read back that value before releasing. */
    (void)SetSelectorLimit((UINT)selector, (DWORD)original_limit);
    if (GetSelectorLimit((UINT)selector) != (DWORD)original_limit) return 0;

    /* Microsoft's Q89560 documents nonzero as the failure result. */
    return FreeSelector((UINT)selector) == 0;
}

int S3_Win16NativeSelectorOps(S3_BRIDGE_OPS S3_FAR *ops)
{
    if (ops == (S3_BRIDGE_OPS S3_FAR *)0) return 0;
    ops->current_code_selector = s3_native_current_code_selector;
    ops->alloc_selector = s3_native_alloc_selector;
    ops->make_data_selector = s3_native_make_data_selector;
    ops->set_selector_base = s3_native_set_selector_base;
    ops->set_selector_limit = s3_native_set_selector_limit;
    ops->get_selector_base = s3_native_get_selector_base;
    ops->get_selector_limit = s3_native_get_selector_limit;
    ops->selector_window = s3_native_selector_window;
    ops->free_selector = s3_native_free_selector;
    return 1;
}
