#ifndef S3_WIN16_BRIDGE_H
#define S3_WIN16_BRIDGE_H

#include "s3_abi.h"

/* Selector and mode operations are injected. The bridge deliberately has no
   default hardware provider, so callers cannot enable a device by accident.
   Every callback returns nonzero on success. alloc_selector returns the
   allocated selector and its exact original limit.
   free_selector must restore/use that limit and translate Win16 FreeSelector's
   documented nonzero-on-failure result accordingly.
   capture_mode only reads state and must not change hardware. A failed Open
   may leave selector_owned set if release failed; retain the bridge and retry
   Close before reinitializing it. */
#define S3_BRIDGE_MODE_STATE_BYTES 1024U
#define S3_BRIDGE_APERTURE_BYTES  65536UL
#define S3_BRIDGE_MAX_BANK        14UL

typedef struct S3_BRIDGE_OPS {
    void S3_FAR *context;
    int (S3_FAR S3_PASCAL *current_code_selector)(void S3_FAR *, S3_U16 S3_FAR *);
    int (S3_FAR S3_PASCAL *alloc_selector)(void S3_FAR *, S3_U16,
                                            S3_U16 S3_FAR *,
                                            S3_U32 S3_FAR *);
    int (S3_FAR S3_PASCAL *make_data_selector)(void S3_FAR *, S3_U16, S3_U16);
    int (S3_FAR S3_PASCAL *set_selector_base)(void S3_FAR *, S3_U16, S3_U32);
    int (S3_FAR S3_PASCAL *set_selector_limit)(void S3_FAR *, S3_U16, S3_U32);
    int (S3_FAR S3_PASCAL *get_selector_base)(void S3_FAR *, S3_U16, S3_U32 S3_FAR *);
    int (S3_FAR S3_PASCAL *get_selector_limit)(void S3_FAR *, S3_U16, S3_U32 S3_FAR *);
    int (S3_FAR S3_PASCAL *selector_window)(void S3_FAR *, S3_U16, void S3_FAR * S3_FAR *);
    int (S3_FAR S3_PASCAL *free_selector)(void S3_FAR *, S3_U16, S3_U32);
    int (S3_FAR S3_PASCAL *capture_mode)(void S3_FAR *, S3_U8 S3_FAR *, S3_U16);
    int (S3_FAR S3_PASCAL *program_mode)(void S3_FAR *);
    int (S3_FAR S3_PASCAL *restore_mode)(void S3_FAR *, const S3_U8 S3_FAR *, S3_U16);
    int (S3_FAR S3_PASCAL *set_bank)(void S3_FAR *, S3_U32);
} S3_BRIDGE_OPS;

typedef struct S3_WIN16_BRIDGE {
    S3_BRIDGE_OPS ops;
    S3_U16 selector;
    S3_U16 selector_owned;
    S3_U16 mode_snapshot_valid;
    S3_U16 mode_active;
    S3_U32 selector_original_limit;
    S3_U32 linear_base;
    S3_U32 max_bank;
    S3_U16 linear_banking;
    void S3_FAR *window;
    S3_U8 mode_snapshot[S3_BRIDGE_MODE_STATE_BYTES];
} S3_WIN16_BRIDGE;

void S3_Win16BridgeInit(S3_WIN16_BRIDGE S3_FAR *bridge);
int S3_Win16BridgeOpen(S3_WIN16_BRIDGE S3_FAR *bridge,
                       const S3_BRIDGE_OPS S3_FAR *ops,
                       S3_U32 linear_base, S3_U32 aperture_bytes);
/* Open over a linear framebuffer mapped at linear_base: banks 0..max_bank
   are reached by moving the selector base in 64 KiB steps instead of
   ops->set_bank, which is still called once for bank 0. */
int S3_Win16BridgeOpenLinear(S3_WIN16_BRIDGE S3_FAR *bridge,
                             const S3_BRIDGE_OPS S3_FAR *ops,
                             S3_U32 linear_base, S3_U32 max_bank);
int S3_Win16BridgeSetBank(S3_WIN16_BRIDGE S3_FAR *bridge, S3_U32 bank);
int S3_Win16BridgeClose(S3_WIN16_BRIDGE S3_FAR *bridge);

#endif
