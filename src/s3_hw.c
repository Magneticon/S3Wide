#include "s3_hw.h"

#define S3_CRTC_REG_LOCK1 0x38
#define S3_CRTC_BANK_LOW  0x35
#define S3_CRTC_BANK_HIGH 0x51
#define S3_CRTC_UNLOCK1  0x48
#define S3_BANK_MAX       63UL
#define S3_DAC_WRITE_INDEX 0x03C8
#define S3_DAC_DATA        0x03C9
#define S3_PLL_REF_KHZ     14318UL
#define S3_MODE_CRTC_LOCK1 0x38
#define S3_MODE_CRTC_UNLOCK1 0x48
#define S3_MODE_SEQ_UNLOCK 0x06

static const S3_U8 s3_mode_extended_regs[S3_MODE_EXT_REG_COUNT] = {
    0x42, 0x43, 0x50, 0x51, 0x5D, 0x5E, 0x67, 0x31, 0x58, 0x35
};

static int s3_valid_mode_io(const S3_MODE_IO S3_FAR *io)
{
    return io != (const S3_MODE_IO S3_FAR *)0 &&
           (io->crtc_index_port == 0x03B4 || io->crtc_index_port == 0x03D4) &&
           io->read_seq != 0 && io->write_seq != 0 &&
           io->read_crtc != 0 && io->write_crtc != 0 &&
           io->read_misc != 0 && io->write_misc != 0;
}

static void s3_pulse_pll(const S3_MODE_IO S3_FAR *io, S3_U8 old_sr15)
{
    S3_U8 base = (S3_U8)(old_sr15 & (S3_U8)~0x20);
    io->write_seq(io->context, 0x15, base);
    io->write_seq(io->context, 0x15, (S3_U8)(base | 0x20));
    io->write_seq(io->context, 0x15, base);
    io->write_seq(io->context, 0x15, old_sr15);
}

static int s3_valid_crtc_io(const S3_REGISTER_IO S3_FAR *io)
{
    return io != (const S3_REGISTER_IO S3_FAR *)0 &&
           (io->crtc_index_port == 0x03B4 || io->crtc_index_port == 0x03D4) &&
           io->read_crtc != 0 && io->write_crtc != 0;
}

int S3_Select64KBank(S3_REGISTER_IO S3_FAR *io, S3_U32 bank)
{
    S3_U8 old_lock, old_low, old_high;
    S3_U8 new_low, new_high;

    if (!s3_valid_crtc_io(io) || bank > S3_BANK_MAX) {
        return 0;
    }

    old_lock = io->read_crtc(io->context, io->crtc_index_port, S3_CRTC_REG_LOCK1);
    old_low = io->read_crtc(io->context, io->crtc_index_port, S3_CRTC_BANK_LOW);
    old_high = io->read_crtc(io->context, io->crtc_index_port, S3_CRTC_BANK_HIGH);
    new_low = (S3_U8)((old_low & 0xF0) | (bank & 0x0F));
    new_high = (S3_U8)((old_high & (S3_U8)~0x0C) | ((bank & 0x30) >> 2));

    /* CR35 writes are ignored unless CR38 has the S3 unlock value. Preserve
       and restore the prior lock state after programming both bank fields. */
    io->write_crtc(io->context, io->crtc_index_port, S3_CRTC_REG_LOCK1, S3_CRTC_UNLOCK1);
    io->write_crtc(io->context, io->crtc_index_port, S3_CRTC_BANK_LOW, new_low);
    io->write_crtc(io->context, io->crtc_index_port, S3_CRTC_BANK_HIGH, new_high);
    io->write_crtc(io->context, io->crtc_index_port, S3_CRTC_REG_LOCK1, old_lock);
    return 1;
}

static S3_U8 s3_expand_bits(S3_U8 value, S3_U16 levels)
{
    return (S3_U8)(((S3_U16)value * 255 + (levels - 1) / 2) / (levels - 1));
}

static S3_U8 s3_rgb8_to_dac6(S3_U8 value)
{
    return (S3_U8)(((S3_U16)value * 63 + 127) / 255);
}

int S3_ProgramRGB332Palette6(S3_REGISTER_IO S3_FAR *io)
{
    S3_U16 index;
    if (io == (S3_REGISTER_IO S3_FAR *)0 || io->write_dac == 0) {
        return 0;
    }

    io->write_dac(io->context, S3_DAC_WRITE_INDEX, 0);
    for (index = 0; index < 256; ++index) {
        S3_U8 red = s3_expand_bits((S3_U8)(index >> 5), 8);
        S3_U8 green = s3_expand_bits((S3_U8)((index >> 2) & 7), 8);
        S3_U8 blue = s3_expand_bits((S3_U8)(index & 3), 4);
        io->write_dac(io->context, S3_DAC_DATA, s3_rgb8_to_dac6(red));
        io->write_dac(io->context, S3_DAC_DATA, s3_rgb8_to_dac6(green));
        io->write_dac(io->context, S3_DAC_DATA, s3_rgb8_to_dac6(blue));
    }
    return 1;
}

int S3_EncodeVideoPLL(S3_U8 m, S3_U8 n, S3_U8 r,
                      S3_U8 S3_FAR *sr12, S3_U8 S3_FAR *sr13)
{
    if (sr12 == (S3_U8 S3_FAR *)0 || sr13 == (S3_U8 S3_FAR *)0 ||
        m > 127 || n > 31 || r > 3) {
        return 0;
    }
    *sr12 = (S3_U8)((r << 5) | n);
    *sr13 = m;
    return 1;
}

int S3_DecodeVideoPLL(S3_U8 sr12, S3_U8 sr13,
                      S3_U8 S3_FAR *m, S3_U8 S3_FAR *n, S3_U8 S3_FAR *r)
{
    if (m == (S3_U8 S3_FAR *)0 || n == (S3_U8 S3_FAR *)0 ||
        r == (S3_U8 S3_FAR *)0 || (sr13 & 0x80) != 0 || (sr12 >> 5) > 3) {
        return 0;
    }
    *n = (S3_U8)(sr12 & 0x1F);
    *r = (S3_U8)(sr12 >> 5);
    *m = (S3_U8)(sr13 & 0x7F);
    return 1;
}

S3_U32 S3_VideoPLLClockKHz(S3_U8 m, S3_U8 n, S3_U8 r)
{
    if (m > 127 || n > 31 || r > 3) {
        return 0;
    }
    return (S3_PLL_REF_KHZ * ((S3_U32)m + 2)) /
           (((S3_U32)n + 2) * (1UL << r));
}

int S3_CaptureModeSnapshot(const S3_MODE_IO S3_FAR *io,
                           S3_MODE_SNAPSHOT S3_FAR *snapshot)
{
    S3_U8 i;
    if (!s3_valid_mode_io(io) || snapshot == (S3_MODE_SNAPSHOT S3_FAR *)0) {
        return 0;
    }
    snapshot->valid = 0;
    snapshot->misc_output = io->read_misc(io->context);
    snapshot->seq01 = io->read_seq(io->context, 0x01);
    snapshot->seq08 = io->read_seq(io->context, 0x08);
    io->write_seq(io->context, 0x08, S3_MODE_SEQ_UNLOCK);
    snapshot->seq12 = io->read_seq(io->context, 0x12);
    snapshot->seq13 = io->read_seq(io->context, 0x13);
    snapshot->seq15 = io->read_seq(io->context, 0x15);
    io->write_seq(io->context, 0x08, snapshot->seq08);
    for (i = 0; i < S3_MODE_CRTC_REG_COUNT; ++i) {
        snapshot->crtc[i] = io->read_crtc(io->context, io->crtc_index_port, i);
    }
    snapshot->crtc38 = io->read_crtc(io->context, io->crtc_index_port,
                                     S3_MODE_CRTC_LOCK1);
    io->write_crtc(io->context, io->crtc_index_port,
                   S3_MODE_CRTC_LOCK1, S3_MODE_CRTC_UNLOCK1);
    for (i = 0; i < S3_MODE_EXT_REG_COUNT; ++i) {
        snapshot->extended[i] = io->read_crtc(io->context,
                                              io->crtc_index_port,
                                              s3_mode_extended_regs[i]);
    }
    snapshot->crtc3a = io->read_crtc(io->context, io->crtc_index_port, 0x3A);
    io->write_crtc(io->context, io->crtc_index_port,
                   S3_MODE_CRTC_LOCK1, snapshot->crtc38);
    snapshot->valid = 1;
    return 1;
}

static void s3_restore_crtc_registers(const S3_MODE_IO S3_FAR *io,
                                      const S3_MODE_SNAPSHOT S3_FAR *snapshot)
{
    S3_U8 i;
    io->write_crtc(io->context, io->crtc_index_port, 0x35,
                   (S3_U8)(io->read_crtc(io->context, io->crtc_index_port,
                                        0x35) & (S3_U8)~0x30));
    io->write_crtc(io->context, io->crtc_index_port, 0x11,
                   (S3_U8)(io->read_crtc(io->context, io->crtc_index_port,
                                        0x11) & 0x7F));
    for (i = 0; i < S3_MODE_CRTC_REG_COUNT; ++i) {
        if (i != 0x11) {
            io->write_crtc(io->context, io->crtc_index_port, i,
                           snapshot->crtc[i]);
        }
    }
    io->write_crtc(io->context, io->crtc_index_port, 0x3A,
                   snapshot->crtc3a);
    for (i = 0; i < S3_MODE_EXT_REG_COUNT; ++i) {
        if (i != 9) {
            io->write_crtc(io->context, io->crtc_index_port,
                           s3_mode_extended_regs[i], snapshot->extended[i]);
        }
    }
    io->write_crtc(io->context, io->crtc_index_port, 0x11,
                   snapshot->crtc[0x11]);
    io->write_crtc(io->context, io->crtc_index_port, 0x35,
                   snapshot->extended[9]);
    io->write_crtc(io->context, io->crtc_index_port, S3_MODE_CRTC_LOCK1,
                   snapshot->crtc38);
}

static int s3_candidate_readback_matches(const S3_MODE_IO S3_FAR *io,
                                          const S3_MODE_SNAPSHOT S3_FAR *snapshot,
                                          S3_U8 sr12, S3_U8 sr13,
                                          S3_U8 cr03, S3_U8 cr05,
                                          S3_U8 cr09,
                                          S3_U8 cr3a,
                                          S3_U8 cr11,
                                          S3_U8 cr17,
                                          S3_U8 cr14, S3_U8 cr16,
                                          S3_U8 cr50,
                                          S3_U8 cr51, S3_U8 cr5d,
                                          S3_U8 cr5e)
{
    return (io->read_misc(io->context) & 0x0C) == 0x0C &&
           io->read_seq(io->context, 0x12) == sr12 &&
           io->read_seq(io->context, 0x13) == sr13 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x00) == 0xCB &&
           io->read_crtc(io->context, io->crtc_index_port, 0x01) == 0x9F &&
           io->read_crtc(io->context, io->crtc_index_port, 0x02) == 0xA0 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x03) == cr03 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x04) == 0xA7 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x05) == cr05 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x06) == 0xE8 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x07) == 0xF0 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x09) == cr09 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x3A) == cr3a &&
           io->read_crtc(io->context, io->crtc_index_port, 0x0C) == 0x00 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x0D) == 0x00 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x10) == 0xD1 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x11) == cr11 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x12) == 0xCF &&
           io->read_crtc(io->context, io->crtc_index_port, 0x13) == 0xA0 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x14) == cr14 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x15) == 0xD0 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x16) == cr16 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x17) == cr17 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x18) == 0xFF &&
           io->read_crtc(io->context, io->crtc_index_port, 0x43) == 0x00 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x50) == cr50 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x51) == cr51 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x5D) == cr5d &&
           io->read_crtc(io->context, io->crtc_index_port, 0x5E) == cr5e &&
           io->read_crtc(io->context, io->crtc_index_port, 0x67) == 0x00 &&
           io->read_crtc(io->context, io->crtc_index_port, 0x31) ==
               (S3_U8)((snapshot->extended[7] & (S3_U8)~0x30) | 0x09) &&
           io->read_crtc(io->context, io->crtc_index_port, 0x58) ==
               (S3_U8)(snapshot->extended[8] & (S3_U8)~0x13);
}

int S3_RestoreModeSnapshot(const S3_MODE_IO S3_FAR *io,
                           const S3_MODE_SNAPSHOT S3_FAR *snapshot)
{
    S3_U8 blank;
    if (!s3_valid_mode_io(io) || snapshot == (const S3_MODE_SNAPSHOT S3_FAR *)0 ||
        snapshot->valid != 1) {
        return 0;
    }
    blank = (S3_U8)(io->read_seq(io->context, 0x01) | 0x20);
    io->write_seq(io->context, 0x01, blank);
    io->write_seq(io->context, 0x08, S3_MODE_SEQ_UNLOCK);
    io->write_crtc(io->context, io->crtc_index_port,
                   S3_MODE_CRTC_LOCK1, S3_MODE_CRTC_UNLOCK1);
    s3_restore_crtc_registers(io, snapshot);
    io->write_seq(io->context, 0x12, snapshot->seq12);
    io->write_seq(io->context, 0x13, snapshot->seq13);
    s3_pulse_pll(io, io->read_seq(io->context, 0x15));
    io->write_misc(io->context, snapshot->misc_output);
    io->write_seq(io->context, 0x15, snapshot->seq15);
    io->write_seq(io->context, 0x08, snapshot->seq08);
    io->write_seq(io->context, 0x01, snapshot->seq01);
    /* Restoring CR31 can make DOSBox-X transition VGA_DetermineMode back to
       text mode; that transition rewrites the CRTC cursor address. Restore
       the captured text cursor last, after the mode and PLL state settle. */
    io->write_crtc(io->context, io->crtc_index_port, 0x0E,
                   snapshot->crtc[0x0E]);
    io->write_crtc(io->context, io->crtc_index_port, 0x0F,
                   snapshot->crtc[0x0F]);
    return 1;
}

int S3_Program720p8bppCandidate(const S3_MODE_IO S3_FAR *io,
                                S3_MODE_SNAPSHOT S3_FAR *snapshot)
{
    S3_U8 cr11, cr11_mode, cr03, cr05, cr09, cr14, cr16, cr17, cr3a, cr50, cr51, cr5d, cr5e, cr67;
    S3_U8 sr12, sr13;
    if (!s3_valid_mode_io(io) || snapshot == (S3_MODE_SNAPSHOT S3_FAR *)0 ||
        !S3_EncodeVideoPLL(50, 3, 1, &sr12, &sr13) ||
        !S3_CaptureModeSnapshot(io, snapshot)) {
        return 0;
    }

    /* This is a timing/PLL experiment path only. It is never called by Enable. */
    io->write_seq(io->context, 0x01, (S3_U8)(snapshot->seq01 | 0x20));
    io->write_seq(io->context, 0x08, S3_MODE_SEQ_UNLOCK);
    io->write_misc(io->context,
                   (S3_U8)((snapshot->misc_output & (S3_U8)~0xCC) | 0xCC));
    io->write_seq(io->context, 0x12, sr12);
    io->write_seq(io->context, 0x13, sr13);
    s3_pulse_pll((S3_MODE_IO S3_FAR *)io, snapshot->seq15);

    io->write_crtc(io->context, io->crtc_index_port,
                   S3_MODE_CRTC_LOCK1, S3_MODE_CRTC_UNLOCK1);
    io->write_crtc(io->context, io->crtc_index_port, 0x35,
                   (S3_U8)(snapshot->extended[9] & (S3_U8)~0x30));
    cr11 = snapshot->crtc[0x11];
    cr11_mode = (S3_U8)((cr11 & 0xF0) | 0x04);
    io->write_crtc(io->context, io->crtc_index_port, 0x11,
                   (S3_U8)(cr11 & 0x7F));
    cr03 = (S3_U8)((snapshot->crtc[0x03] & 0xE0) | 0x10);
    cr05 = (S3_U8)((snapshot->crtc[0x05] & 0x60) | 0x18);
    io->write_crtc(io->context, io->crtc_index_port, 0x00, 0xCB);
    io->write_crtc(io->context, io->crtc_index_port, 0x01, 0x9F);
    io->write_crtc(io->context, io->crtc_index_port, 0x02, 0xA0);
    io->write_crtc(io->context, io->crtc_index_port, 0x03, cr03);
    io->write_crtc(io->context, io->crtc_index_port, 0x04, 0xA7);
    io->write_crtc(io->context, io->crtc_index_port, 0x05, cr05);
    io->write_crtc(io->context, io->crtc_index_port, 0x06, 0xE8);
    io->write_crtc(io->context, io->crtc_index_port, 0x07, 0xF0);
    /* 720 active lines and a vertical blank start above line 511 require
       CR09[6:5]=11; disable doublescan and per-character row repetition. */
    cr09 = 0x60;
    io->write_crtc(io->context, io->crtc_index_port, 0x09, cr09);
    io->write_crtc(io->context, io->crtc_index_port, 0x10, 0xD1);
    io->write_crtc(io->context, io->crtc_index_port, 0x11,
                   cr11_mode);
    io->write_crtc(io->context, io->crtc_index_port, 0x12, 0xCF);
    io->write_crtc(io->context, io->crtc_index_port, 0x13, 0xA0);
    cr14 = (S3_U8)(snapshot->crtc[0x14] | 0x40);
    io->write_crtc(io->context, io->crtc_index_port, 0x14, cr14);
    io->write_crtc(io->context, io->crtc_index_port, 0x15, 0xD0);
    /* DOSBox-X's VGA wrap model uses CR16=FF to end vertical blanking
       without carrying an initial-frame skip into the active display. */
    cr16 = 0xFF;
    io->write_crtc(io->context, io->crtc_index_port, 0x16, cr16);
    /* Preserve the captured CRTC byte/word address mode (CR17[6]); only
       select the intended display timing and count-by-two behavior. */
    cr17 = (S3_U8)(0xA3 | (snapshot->crtc[0x17] & 0x40));
    io->write_crtc(io->context, io->crtc_index_port, 0x17, cr17);
    io->write_crtc(io->context, io->crtc_index_port, 0x18, 0xFF);
    io->write_crtc(io->context, io->crtc_index_port, 0x0C, 0x00);
    io->write_crtc(io->context, io->crtc_index_port, 0x0D, 0x00);

    /* Trio32/64 documentation reserves CR42[4:0]; CR42 bit 5 selects
       interlace. Leave interlace off and reserved bits zero. */
    io->write_crtc(io->context, io->crtc_index_port, 0x42, 0x00);
    io->write_crtc(io->context, io->crtc_index_port, 0x43, 0x00);
    /* Trio32/64 CR3A[4] selects 8/16/24/32-bpp mode operation. */
    cr3a = (S3_U8)(snapshot->crtc3a | 0x10);
    io->write_crtc(io->context, io->crtc_index_port, 0x3A, cr3a);
    cr50 = (S3_U8)((snapshot->extended[2] & (S3_U8)~0xF1) | 0xC0);
    io->write_crtc(io->context, io->crtc_index_port, 0x50, cr50);
    /* CR51[5:4] are offset bits 8-9, not width bits. For 1280-byte
       pitch CR13 is 160 (0xA0), so both extension bits must be zero. */
    cr51 = (S3_U8)(snapshot->extended[3] & (S3_U8)~0x33);
    io->write_crtc(io->context, io->crtc_index_port, 0x51, cr51);
    cr5d = (S3_U8)(snapshot->extended[4] & (S3_U8)~0x17);
    cr5e = (S3_U8)(snapshot->extended[5] & (S3_U8)~0x57);
    io->write_crtc(io->context, io->crtc_index_port, 0x5D, cr5d);
    io->write_crtc(io->context, io->crtc_index_port, 0x5E, cr5e);
    cr67 = 0x00;
    io->write_crtc(io->context, io->crtc_index_port, 0x67, cr67);
    io->write_crtc(io->context, io->crtc_index_port, 0x31,
                   (S3_U8)((snapshot->extended[7] & (S3_U8)~0x30) | 0x09));
    io->write_crtc(io->context, io->crtc_index_port, 0x58,
                   (S3_U8)(snapshot->extended[8] & (S3_U8)~0x13));
    io->write_crtc(io->context, io->crtc_index_port, 0x35,
                   snapshot->extended[9]);
    if (!s3_candidate_readback_matches(io, snapshot, sr12, sr13, cr03, cr05,
                                       cr09, cr3a, cr11_mode, cr17, cr14, cr16, cr50, cr51, cr5d,
                                       cr5e)) {
        (void)S3_RestoreModeSnapshot(io, snapshot);
        return 0;
    }
    io->write_crtc(io->context, io->crtc_index_port, 0x38,
                   snapshot->crtc38);
    io->write_seq(io->context, 0x15, snapshot->seq15);
    io->write_seq(io->context, 0x08, snapshot->seq08);
    io->write_seq(io->context, 0x01, snapshot->seq01);
    return 1;
}

int S3_Encode8BppPitch(S3_U16 pitch, S3_U8 S3_FAR *crtc_offset)
{
    S3_U16 offset;
    if (crtc_offset == (S3_U8 S3_FAR *)0 || pitch == 0 || (pitch & 7) != 0) {
        return 0;
    }
    offset = (S3_U16)(pitch / 8);
    if (offset > 255) {
        return 0;
    }
    *crtc_offset = (S3_U8)offset;
    return 1;
}

int S3_Decode8BppPitch(S3_U8 crtc_offset, S3_U16 S3_FAR *pitch)
{
    if (pitch == (S3_U16 S3_FAR *)0) {
        return 0;
    }
    *pitch = (S3_U16)crtc_offset * 8;
    return 1;
}
