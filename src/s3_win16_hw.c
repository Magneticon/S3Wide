#include "s3_win16_hw.h"
#include "s3_hw.h"
#include "s3_trace.h"
#include <conio.h>

#define S3_SEQ_COUNT 5
#define S3_GC_COUNT 9
#define S3_AC_COUNT 21
#define S3_DAC_BYTES 768

/* Saved in the bridge's mode-state buffer by capture_mode. */
typedef struct S3_SAVED_VGA {
    S3_MODE_SNAPSHOT timing;
    S3_U8 seq[S3_SEQ_COUNT];
    S3_U8 gc[S3_GC_COUNT];
    S3_U8 ac[S3_AC_COUNT];
    S3_U8 dac_mask;
    S3_U8 dac[S3_DAC_BYTES];
} S3_SAVED_VGA;

typedef char s3_saved_vga_fits[(sizeof(S3_SAVED_VGA) <= S3_BRIDGE_MODE_STATE_BYTES) ? 1 : -1];

typedef struct S3_PORT_PROVIDER {
    S3_MODE_IO mode_io;
    S3_REGISTER_IO register_io;
    S3_MODE_SNAPSHOT timing;
} S3_PORT_PROVIDER;

static S3_PORT_PROVIDER s3_port_provider;

static S3_U8 S3_FAR S3_PASCAL s3_read_crtc(void S3_FAR *context,
                                           S3_U16 index_port, S3_U8 index)
{
    (void)context;
    outp(index_port, index);
    return (S3_U8)inp(index_port + 1);
}

static void S3_FAR S3_PASCAL s3_write_crtc(void S3_FAR *context,
                                           S3_U16 index_port, S3_U8 index,
                                           S3_U8 value)
{
    (void)context;
    outp(index_port, index);
    outp(index_port + 1, value);
}

static S3_U8 S3_FAR S3_PASCAL s3_read_seq(void S3_FAR *context, S3_U8 index)
{
    (void)context;
    outp(0x3C4, index);
    return (S3_U8)inp(0x3C5);
}

static void S3_FAR S3_PASCAL s3_write_seq(void S3_FAR *context, S3_U8 index,
                                          S3_U8 value)
{
    (void)context;
    outp(0x3C4, index);
    outp(0x3C5, value);
}

static S3_U8 S3_FAR S3_PASCAL s3_read_misc(void S3_FAR *context)
{
    (void)context;
    return (S3_U8)inp(0x3CC);
}

static void S3_FAR S3_PASCAL s3_write_misc(void S3_FAR *context, S3_U8 value)
{
    (void)context;
    outp(0x3C2, value);
}

static void S3_FAR S3_PASCAL s3_write_dac(void S3_FAR *context, S3_U16 port,
                                          S3_U8 value)
{
    (void)context;
    outp(port, value);
}

/* Reading 3DAh resets the attribute controller's index/data flip-flop.
   Writing an index without bit 5 blanks the display until 20h is written. */
static S3_U8 s3_read_ac(S3_U8 index)
{
    (void)inp(0x3DA);
    outp(0x3C0, index);
    return (S3_U8)inp(0x3C1);
}

static void s3_write_ac(S3_U8 index, S3_U8 value)
{
    (void)inp(0x3DA);
    outp(0x3C0, index);
    outp(0x3C0, value);
}

static void s3_enable_ac_display(void)
{
    (void)inp(0x3DA);
    outp(0x3C0, 0x20);
}

static int S3_FAR S3_PASCAL s3_capture_mode(void S3_FAR *context,
                                            S3_U8 S3_FAR *buffer, S3_U16 bytes)
{
    S3_PORT_PROVIDER S3_FAR *provider = (S3_PORT_PROVIDER S3_FAR *)context;
    S3_SAVED_VGA S3_FAR *saved = (S3_SAVED_VGA S3_FAR *)buffer;
    S3_U16 i;

    if (provider == (S3_PORT_PROVIDER S3_FAR *)0 ||
        saved == (S3_SAVED_VGA S3_FAR *)0 || bytes < sizeof(S3_SAVED_VGA)) {
        return 0;
    }
    provider->mode_io.crtc_index_port = (inp(0x3CC) & 1) ? 0x3D4 : 0x3B4;
    provider->register_io.crtc_index_port = provider->mode_io.crtc_index_port;
    if (!S3_CaptureModeSnapshot(&provider->mode_io, &provider->timing)) {
        return 0;
    }
    saved->timing = provider->timing;
    for (i = 0; i < S3_SEQ_COUNT; ++i) {
        saved->seq[i] = s3_read_seq(0, (S3_U8)i);
    }
    for (i = 0; i < S3_GC_COUNT; ++i) {
        outp(0x3CE, (S3_U8)i);
        saved->gc[i] = (S3_U8)inp(0x3CF);
    }
    for (i = 0; i < S3_AC_COUNT; ++i) {
        saved->ac[i] = s3_read_ac((S3_U8)i);
    }
    s3_enable_ac_display();
    saved->dac_mask = (S3_U8)inp(0x3C6);
    outp(0x3C7, 0);
    for (i = 0; i < S3_DAC_BYTES; ++i) {
        saved->dac[i] = (S3_U8)inp(0x3C9);
    }
    S3_TRACE_EVENT("hw.capture", provider->mode_io.crtc_index_port,
                   saved->timing.misc_output);
    return 1;
}

/* Packed 256-colour addressing at A000:0, as in raw_s3_cold_smoke.c. */
static void s3_setup_packed_8bpp(void)
{
    static const S3_U8 gc[S3_GC_COUNT] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF
    };
    S3_U8 i;

    s3_write_seq(0, 0x00, 0x01);
    s3_write_seq(0, 0x01, 0x01);
    s3_write_seq(0, 0x02, 0x0F);
    s3_write_seq(0, 0x03, 0x00);
    s3_write_seq(0, 0x04, 0x0E);
    s3_write_seq(0, 0x00, 0x03);
    for (i = 0; i < S3_GC_COUNT; ++i) {
        outp(0x3CE, i);
        outp(0x3CF, gc[i]);
    }
    for (i = 0; i < 16; ++i) {
        s3_write_ac(i, i);
    }
    s3_write_ac(0x10, 0x41);
    s3_write_ac(0x11, 0x00);
    s3_write_ac(0x12, 0x0F);
    s3_write_ac(0x13, 0x00);
    s3_write_ac(0x14, 0x00);
    s3_enable_ac_display();
}

static int S3_FAR S3_PASCAL s3_program_mode(void S3_FAR *context)
{
    S3_PORT_PROVIDER S3_FAR *provider = (S3_PORT_PROVIDER S3_FAR *)context;

    if (provider == (S3_PORT_PROVIDER S3_FAR *)0 || !provider->timing.valid) {
        return 0;
    }
    /* The candidate rolls its own registers back if verification fails. */
    if (!S3_Program720p8bppCandidate(&provider->mode_io, &provider->timing)) {
        S3_TRACE_EVENT("hw.program.fail", 1, 0);
        return 0;
    }
    s3_setup_packed_8bpp();
    if (!S3_ProgramRGB332Palette6(&provider->register_io)) {
        S3_TRACE_EVENT("hw.program.fail", 2, 0);
        return 0;
    }
    S3_TRACE_EVENT("hw.program.ok", 0, 0);
    return 1;
}

static int S3_FAR S3_PASCAL s3_restore_mode(void S3_FAR *context,
                                            const S3_U8 S3_FAR *buffer,
                                            S3_U16 bytes)
{
    S3_PORT_PROVIDER S3_FAR *provider = (S3_PORT_PROVIDER S3_FAR *)context;
    const S3_SAVED_VGA S3_FAR *saved = (const S3_SAVED_VGA S3_FAR *)buffer;
    S3_U16 i;

    if (provider == (S3_PORT_PROVIDER S3_FAR *)0 ||
        saved == (const S3_SAVED_VGA S3_FAR *)0 ||
        bytes < sizeof(S3_SAVED_VGA) || !saved->timing.valid) {
        return 0;
    }
    if (!S3_RestoreModeSnapshot(&provider->mode_io, &saved->timing)) {
        S3_TRACE_EVENT("hw.restore.fail", 1, 0);
        return 0;
    }
    s3_write_seq(0, 0x00, 0x01);
    for (i = 1; i < S3_SEQ_COUNT; ++i) {
        s3_write_seq(0, (S3_U8)i, saved->seq[i]);
    }
    s3_write_seq(0, 0x00, saved->seq[0]);
    for (i = 0; i < S3_GC_COUNT; ++i) {
        outp(0x3CE, (S3_U8)i);
        outp(0x3CF, saved->gc[i]);
    }
    for (i = 0; i < S3_AC_COUNT; ++i) {
        s3_write_ac((S3_U8)i, saved->ac[i]);
    }
    s3_enable_ac_display();
    outp(0x3C6, saved->dac_mask);
    outp(0x3C8, 0);
    for (i = 0; i < S3_DAC_BYTES; ++i) {
        outp(0x3C9, saved->dac[i]);
    }
    S3_TRACE_EVENT("hw.restore.ok", 0, 0);
    return 1;
}

static int S3_FAR S3_PASCAL s3_set_bank(void S3_FAR *context, S3_U32 bank)
{
    S3_PORT_PROVIDER S3_FAR *provider = (S3_PORT_PROVIDER S3_FAR *)context;
    if (provider == (S3_PORT_PROVIDER S3_FAR *)0) return 0;
    return S3_Select64KBank(&provider->register_io, bank);
}

int S3_Win16PortModeOps(S3_BRIDGE_OPS S3_FAR *ops)
{
    S3_PORT_PROVIDER S3_FAR *provider = &s3_port_provider;

    if (ops == (S3_BRIDGE_OPS S3_FAR *)0) return 0;
    provider->mode_io.context = provider;
    provider->mode_io.crtc_index_port = 0x3D4;
    provider->mode_io.read_seq = s3_read_seq;
    provider->mode_io.write_seq = s3_write_seq;
    provider->mode_io.read_crtc = s3_read_crtc;
    provider->mode_io.write_crtc = s3_write_crtc;
    provider->mode_io.read_misc = s3_read_misc;
    provider->mode_io.write_misc = s3_write_misc;
    provider->register_io.crtc_index_port = 0x3D4;
    provider->register_io.context = provider;
    provider->register_io.read_crtc = s3_read_crtc;
    provider->register_io.write_crtc = s3_write_crtc;
    provider->register_io.write_dac = s3_write_dac;

    ops->context = provider;
    ops->capture_mode = s3_capture_mode;
    ops->program_mode = s3_program_mode;
    ops->restore_mode = s3_restore_mode;
    ops->set_bank = s3_set_bank;
    return 1;
}
