#include "s3_win16_bridge.h"
#include <string.h>

static int s3_bridge_ops_valid(const S3_BRIDGE_OPS S3_FAR *ops)
{
    return ops != (const S3_BRIDGE_OPS S3_FAR *)0 &&
           ops->current_code_selector != 0 && ops->alloc_selector != 0 &&
           ops->make_data_selector != 0 && ops->set_selector_base != 0 &&
           ops->set_selector_limit != 0 && ops->get_selector_base != 0 &&
           ops->get_selector_limit != 0 && ops->selector_window != 0 &&
           ops->free_selector != 0 && ops->capture_mode != 0 &&
           ops->program_mode != 0 && ops->restore_mode != 0 &&
           ops->set_bank != 0;
}

void S3_Win16BridgeInit(S3_WIN16_BRIDGE S3_FAR *bridge)
{
    if (bridge != (S3_WIN16_BRIDGE S3_FAR *)0) {
        memset(bridge, 0, sizeof(*bridge));
    }
}

static int s3_bridge_release_selector(S3_WIN16_BRIDGE S3_FAR *bridge)
{
    if (!bridge->selector_owned) return 1;
    if (!bridge->ops.free_selector(bridge->ops.context, bridge->selector,
                                   bridge->selector_original_limit)) {
        return 0;
    }
    bridge->selector = 0;
    bridge->selector_owned = 0;
    bridge->selector_original_limit = 0;
    bridge->window = (void S3_FAR *)0;
    if (!bridge->mode_snapshot_valid) bridge->linear_base = 0;
    return 1;
}

static int s3_bridge_rollback_and_release(S3_WIN16_BRIDGE S3_FAR *bridge)
{
    if (bridge->mode_snapshot_valid) {
        if (!bridge->ops.restore_mode(bridge->ops.context,
                                      bridge->mode_snapshot,
                                      S3_BRIDGE_MODE_STATE_BYTES)) {
            return 0;
        }
        bridge->mode_snapshot_valid = 0;
        bridge->mode_active = 0;
        bridge->window = (void S3_FAR *)0;
        bridge->linear_base = 0;
    }
    return s3_bridge_release_selector(bridge);
}

static void s3_bridge_clear_unowned_attempt(
    S3_WIN16_BRIDGE S3_FAR *bridge)
{
    if (bridge->selector_owned || bridge->mode_snapshot_valid) return;
    bridge->selector = 0;
    bridge->mode_active = 0;
    bridge->selector_original_limit = 0;
    bridge->linear_base = 0;
    bridge->window = (void S3_FAR *)0;
    memset(&bridge->ops, 0, sizeof(bridge->ops));
    memset(bridge->mode_snapshot, 0, sizeof(bridge->mode_snapshot));
}

static int s3_bridge_open(S3_WIN16_BRIDGE S3_FAR *bridge,
                          const S3_BRIDGE_OPS S3_FAR *ops,
                          S3_U32 linear_base, S3_U32 aperture_bytes,
                          S3_U32 max_bank, S3_U16 linear_banking)
{
    S3_U16 code_selector, selector;
    S3_U32 original_limit;
    S3_U32 observed_base, observed_limit;
    if (bridge == (S3_WIN16_BRIDGE S3_FAR *)0 ||
        !s3_bridge_ops_valid(ops) || bridge->selector_owned ||
        bridge->mode_snapshot_valid || bridge->mode_active ||
        linear_base == 0 || aperture_bytes != S3_BRIDGE_APERTURE_BYTES ||
        linear_base > 0xFFFFFFFFUL - (aperture_bytes - 1UL) ||
        (linear_banking &&
         (max_bank > 0xFFFFUL ||
          linear_base > 0xFFFFFFFFUL - (max_bank << 16) - 0xFFFFUL))) {
        return 0;
    }
    bridge->ops = *ops;
    bridge->linear_base = linear_base;
    bridge->max_bank = max_bank;
    bridge->linear_banking = linear_banking;
    if (!bridge->ops.current_code_selector(bridge->ops.context,
                                           &code_selector) ||
        code_selector == 0 ||
        !bridge->ops.alloc_selector(bridge->ops.context, code_selector,
                                    &selector, &original_limit) ||
        selector == 0) {
        s3_bridge_clear_unowned_attempt(bridge);
        return 0;
    }
    bridge->selector = selector;
    bridge->selector_owned = 1;
    bridge->selector_original_limit = original_limit;
    if (original_limit > 0xFFFFUL) {
        (void)s3_bridge_release_selector(bridge);
        s3_bridge_clear_unowned_attempt(bridge);
        return 0;
    }
    if (!bridge->ops.make_data_selector(bridge->ops.context,
                                        code_selector, selector) ||
        !bridge->ops.set_selector_base(bridge->ops.context, selector,
                                       linear_base) ||
        !bridge->ops.set_selector_limit(bridge->ops.context, selector,
                                        aperture_bytes - 1UL) ||
        !bridge->ops.get_selector_base(bridge->ops.context, selector,
                                       &observed_base) ||
        !bridge->ops.get_selector_limit(bridge->ops.context, selector,
                                        &observed_limit) ||
        observed_base != linear_base ||
        observed_limit != aperture_bytes - 1UL ||
        !bridge->ops.selector_window(bridge->ops.context, selector,
                                     &bridge->window) ||
        bridge->window == (void S3_FAR *)0) {
        bridge->window = (void S3_FAR *)0;
        bridge->linear_base = 0;
        (void)s3_bridge_release_selector(bridge);
        s3_bridge_clear_unowned_attempt(bridge);
        return 0;
    }
    if (!bridge->ops.capture_mode(bridge->ops.context,
                                  bridge->mode_snapshot,
                                  S3_BRIDGE_MODE_STATE_BYTES)) {
        bridge->window = (void S3_FAR *)0;
        bridge->linear_base = 0;
        (void)s3_bridge_release_selector(bridge);
        s3_bridge_clear_unowned_attempt(bridge);
        return 0;
    }
    bridge->mode_snapshot_valid = 1;
    if (!bridge->ops.program_mode(bridge->ops.context) ||
        !bridge->ops.set_bank(bridge->ops.context, 0)) {
        if (s3_bridge_rollback_and_release(bridge)) {
            bridge->linear_base = 0;
            s3_bridge_clear_unowned_attempt(bridge);
            return 0;
        }
        if (!bridge->mode_snapshot_valid) {
            bridge->window = (void S3_FAR *)0;
            bridge->linear_base = 0;
        }
        /* Retain every handle and the saved mode when rollback or cleanup
           fails so the owner can retry Close instead of losing recovery. */
        return 0;
    }
    bridge->mode_active = 1;
    return 1;
}

int S3_Win16BridgeOpen(S3_WIN16_BRIDGE S3_FAR *bridge,
                       const S3_BRIDGE_OPS S3_FAR *ops,
                       S3_U32 linear_base, S3_U32 aperture_bytes)
{
    return s3_bridge_open(bridge, ops, linear_base, aperture_bytes,
                          S3_BRIDGE_MAX_BANK, 0);
}

int S3_Win16BridgeOpenLinear(S3_WIN16_BRIDGE S3_FAR *bridge,
                             const S3_BRIDGE_OPS S3_FAR *ops,
                             S3_U32 linear_base, S3_U32 max_bank)
{
    return s3_bridge_open(bridge, ops, linear_base, S3_BRIDGE_APERTURE_BYTES,
                          max_bank, 1);
}

int S3_Win16BridgeSetBank(S3_WIN16_BRIDGE S3_FAR *bridge, S3_U32 bank)
{
    if (bridge == (S3_WIN16_BRIDGE S3_FAR *)0 || !bridge->mode_active ||
        !bridge->selector_owned || bridge->window == (void S3_FAR *)0 ||
        bank > bridge->max_bank) {
        return 0;
    }
    if (bridge->linear_banking) {
        return bridge->ops.set_selector_base(bridge->ops.context,
                                             bridge->selector,
                                             bridge->linear_base + (bank << 16));
    }
    return bridge->ops.set_bank(bridge->ops.context, bank);
}

int S3_Win16BridgeClose(S3_WIN16_BRIDGE S3_FAR *bridge)
{
    if (bridge == (S3_WIN16_BRIDGE S3_FAR *)0) return 0;
    if (bridge->mode_snapshot_valid) {
        if (!bridge->ops.restore_mode(bridge->ops.context,
                                      bridge->mode_snapshot,
                                      S3_BRIDGE_MODE_STATE_BYTES)) {
            return 0;
        }
        bridge->mode_snapshot_valid = 0;
        bridge->mode_active = 0;
        bridge->window = (void S3_FAR *)0;
        bridge->linear_base = 0;
    }
    if (!s3_bridge_release_selector(bridge)) return 0;
    bridge->linear_base = 0;
    memset(&bridge->ops, 0, sizeof(bridge->ops));
    memset(bridge->mode_snapshot, 0, sizeof(bridge->mode_snapshot));
    return 1;
}
