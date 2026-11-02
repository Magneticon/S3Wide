/* S3PREP: make S3WIDE.DRV from S3WIDE.BIN and the user's own VGA.DRV.

   S3WIDE.DRV needs the cursors, icons, window-frame bitmaps and OEM tables
   that every Windows 3.1 display driver carries. Those are Microsoft's, so
   the release ships the driver without them (S3WIDE.BIN) and this program
   copies them from the VGA.DRV of the user's own Windows installation:
   it rewrites the NE file with a resource table that holds S3WIDE.BIN's
   version resource followed by every VGA.DRV resource but its version.

   It also copies the files Windows Setup installs with a VGA-style display
   (grabber, logo, fonts) from the same folder into the package folder, so
   Setup finds them there instead of asking for Windows disks.

   Usage:  S3PREP <WINDOWS folder | path of VGA.DRV>
   S3WIDE.BIN is read from, and S3WIDE.DRV and the copied files are written
   to, the folder that holds S3PREP.EXE (the package folder).

   Plain C89 and stdio: built for DOS with Open Watcom (-bt=dos -ml) and
   for the host to test it. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HEADER 8192U
#define MAX_TYPES 32
#define MAX_RES 256
#define MAX_SEGS 64
#define NAME_BYTES 1024
#define RES_SHIFT 4

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

static u16 get16(const u8 *p) { return (u16)(p[0] | ((u16)p[1] << 8)); }
static u32 get32(const u8 *p) { return (u32)get16(p) | ((u32)get16(p + 2) << 16); }
static void put16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void put32(u8 *p, u32 v) { put16(p, (u16)v); put16(p + 2, (u16)(v >> 16)); }

typedef struct {
    u16 type_id;        /* 0x8000 | n, or 0 when named */
    u16 type_name;      /* offset into names[] when named */
    u16 res_id;
    u16 res_name;
    u16 flags;
    u32 offset, bytes;  /* in the source file */
    int from_vga;
} RESOURCE;

typedef struct {
    FILE *f;
    u32 ne;             /* NE header offset */
    u8 hdr[MAX_HEADER]; /* NE header and the tables after it */
    u16 hdr_bytes;
} NEFILE;

static RESOURCE res[MAX_RES];
static int res_count;
static u8 names[NAME_BYTES];   /* length-prefixed names, as in an NE table */
static u16 names_used;
static u8 buf[4096];

static int fail(const char *a, const char *b)
{
    printf("S3PREP: %s%s\n", a, b ? b : "");
    return 0;
}

static int read_at(FILE *f, u32 offset, void *to, u16 bytes)
{
    return fseek(f, (long)offset, SEEK_SET) == 0 && fread(to, 1, bytes, f) == bytes;
}

static int copy_bytes(FILE *in, u32 offset, u32 bytes, FILE *out)
{
    if (fseek(in, (long)offset, SEEK_SET) != 0) return 0;
    while (bytes != 0) {
        u16 n = (u16)(bytes < sizeof(buf) ? bytes : sizeof(buf));
        if (fread(buf, 1, n, in) != n || fwrite(buf, 1, n, out) != n) return 0;
        bytes -= n;
    }
    return 1;
}

static int pad_to(FILE *out, u32 align)
{
    long pos = ftell(out);
    static const u8 zero[16] = {0};
    if (pos < 0) return 0;
    while ((u32)pos % align != 0) {
        u16 n = (u16)(align - (u32)pos % align);
        if (n > sizeof(zero)) n = sizeof(zero);
        if (fwrite(zero, 1, n, out) != n) return 0;
        pos += n;
    }
    return 1;
}

static int open_ne(NEFILE *ne, const char *path)
{
    u8 mz[64];
    u16 end;
    ne->f = fopen(path, "rb");
    if (ne->f == NULL) return fail("cannot open ", path);
    if (!read_at(ne->f, 0, mz, 64)) return fail("cannot read ", path);
    if (mz[0] == 'S' && mz[1] == 'Z' && mz[2] == 'D' && mz[3] == 'D') {
        return fail(path, " is compressed: unpack it first with EXPAND (see INSTALL.TXT)");
    }
    if (mz[0] != 'M' || mz[1] != 'Z') return fail("not a Windows driver: ", path);
    ne->ne = get32(mz + 0x3C);
    if (!read_at(ne->f, ne->ne, ne->hdr, 64) || ne->hdr[0] != 'N' || ne->hdr[1] != 'E') {
        return fail("not a Windows driver: ", path);
    }
    end = (u16)(get16(ne->hdr + 0x04) + get16(ne->hdr + 0x06));
    if (end < 64 || end > MAX_HEADER) return fail("unexpected header in ", path);
    if (!read_at(ne->f, ne->ne, ne->hdr, end)) return fail("cannot read ", path);
    ne->hdr_bytes = end;
    return 1;
}

/* Copy a length-prefixed name from an NE table into names[]. */
static int add_name(const u8 *table, u16 offset, u16 limit, u16 *out)
{
    u16 n;
    if (offset >= limit) return 0;
    n = table[offset];
    if ((u32)offset + 1 + n > limit || names_used + 1U + n > NAME_BYTES) return 0;
    memcpy(names + names_used, table + offset, (size_t)n + 1);
    *out = names_used;
    names_used = (u16)(names_used + 1 + n);
    return 1;
}

/* Collect the resources of one file (all, or all but RT_VERSION). */
static int collect(NEFILE *ne, int from_vga, int skip_version, const char *path)
{
    const u8 *h = ne->hdr;
    u16 rt = get16(h + 0x24), limit = get16(h + 0x26), p, shift, count, i;
    if (rt == limit) return 1; /* no resource table */
    if (rt < 64 || limit > ne->hdr_bytes || rt + 2U > limit) return fail("bad resources in ", path);
    shift = get16(h + rt);
    p = (u16)(rt + 2);
    for (;;) {
        u16 type, type_name = 0;
        if (p + 2U > limit) return fail("bad resources in ", path);
        type = get16(h + p);
        if (type == 0) break;
        if (p + 8U > limit) return fail("bad resources in ", path);
        count = get16(h + p + 2);
        if ((u32)p + 8 + (u32)count * 12 > limit) return fail("bad resources in ", path);
        if (!(type & 0x8000) && !add_name(h + rt, type, (u16)(limit - rt), &type_name)) {
            return fail("bad resource name in ", path);
        }
        for (i = 0; i < count; ++i) {
            const u8 *r = h + p + 8 + i * 12;
            RESOURCE *out;
            if (skip_version && type == 0x8010) continue;
            if (res_count >= MAX_RES) return fail("too many resources in ", path);
            out = &res[res_count];
            out->type_id = (type & 0x8000) ? type : 0;
            out->type_name = type_name;
            out->res_id = get16(r + 6);
            out->res_name = 0;
            if (!(out->res_id & 0x8000)) {
                if (!add_name(h + rt, out->res_id, (u16)(limit - rt), &out->res_name)) {
                    return fail("bad resource name in ", path);
                }
                out->res_id = 0;
            }
            out->flags = get16(r + 4);
            out->offset = (u32)get16(r) << shift;
            out->bytes = (u32)get16(r + 2) << shift;
            out->from_vga = from_vga;
            ++res_count;
        }
        p = (u16)(p + 8 + count * 12);
    }
    return 1;
}

static int same_type(const RESOURCE *a, const RESOURCE *b)
{
    if (a->type_id != 0 || b->type_id != 0) return a->type_id == b->type_id;
    return names[a->type_name] == names[b->type_name] &&
           memcmp(names + a->type_name + 1, names + b->type_name + 1, names[a->type_name]) == 0;
}

/* Build the resource table: types in order of first appearance. Offsets
   are filled in later (restab_fix). Returns its length. */
static u16 build_restab(u8 *t, u16 *slot_of, u16 *name_base)
{
    static int done[MAX_RES];
    u16 p = 2, name_at, type_pos[MAX_TYPES];
    int i, j, types = 0;
    u16 type_name_slot[MAX_TYPES];
    put16(t, RES_SHIFT);
    memset(done, 0, sizeof(done));
    for (i = 0; i < res_count; ++i) {
        u16 count = 0;
        if (done[i]) continue;
        for (j = i; j < res_count; ++j) if (!done[j] && same_type(&res[i], &res[j])) ++count;
        type_pos[types] = p;
        type_name_slot[types] = (u16)i;
        ++types;
        put16(t + p, res[i].type_id);
        put16(t + p + 2, count);
        put32(t + p + 4, 0);
        p = (u16)(p + 8);
        for (j = i; j < res_count; ++j) {
            if (done[j] || !same_type(&res[i], &res[j])) continue;
            done[j] = 1;
            slot_of[j] = p;
            put16(t + p, 0);
            put16(t + p + 2, 0);
            put16(t + p + 4, res[j].flags);
            put16(t + p + 6, res[j].res_id);
            put32(t + p + 8, 0);
            p = (u16)(p + 12);
        }
    }
    put16(t + p, 0);
    p = (u16)(p + 2);
    /* Names follow the table; offsets are from the table's start. */
    *name_base = p;
    memcpy(t + p, names, names_used);
    name_at = p;
    p = (u16)(p + names_used);
    t[p++] = 0;
    for (i = 0; i < types; ++i) {
        const RESOURCE *r = &res[type_name_slot[i]];
        if (r->type_id == 0) put16(t + type_pos[i], (u16)(name_at + r->type_name));
    }
    for (i = 0; i < res_count; ++i) {
        if (res[i].res_id == 0) put16(t + slot_of[i] + 6, (u16)(name_at + res[i].res_name));
    }
    return p;
}

static void dir_of(const char *path, char *out, size_t size)
{
    const char *cut = NULL, *p;
    size_t n;
    for (p = path; *p; ++p) if (*p == '\\' || *p == '/' || *p == ':') cut = p + 1;
    n = cut ? (size_t)(cut - path) : 0;
    if (n >= size) n = size - 1;
    memcpy(out, path, n);
    out[n] = 0;
}

static void join(char *out, size_t size, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    if (n + strlen(name) + 2 > size) {
        out[0] = 0;
        return;
    }
    strcpy(out, dir);
    if (n && dir[n - 1] != '\\' && dir[n - 1] != '/' && dir[n - 1] != ':') strcat(out, "\\");
    strcat(out, name);
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

static int make_driver(const char *bin_path, const char *vga_path, const char *out_path)
{
    static NEFILE bin, vga;
    static u8 hdr[MAX_HEADER];
    static u8 restab[MAX_HEADER];
    static u16 slot_of[MAX_RES];
    static u8 segs[MAX_SEGS * 8];
    FILE *out;
    u16 nseg, old_rt, old_rtend, rtlen, name_base, delta, i, shift;
    u16 tail_from, tail_to;
    u32 nonres_size, nonres_at, pos;
    int r, ok = 0;

    res_count = 0;
    names_used = 0;
    if (!open_ne(&bin, bin_path) || !open_ne(&vga, vga_path)) return 0;
    if (!collect(&bin, 0, 0, bin_path) || !collect(&vga, 1, 1, vga_path)) return 0;
    for (r = 0; r < res_count && !res[r].from_vga; ++r) {}
    if (r == res_count) return fail("no resources found in ", vga_path);

    /* S3WIDE.BIN layout (Open Watcom): segment table, resource table,
       resident names, module references, imported names, entry table. */
    nseg = get16(bin.hdr + 0x1C);
    old_rt = get16(bin.hdr + 0x24);
    old_rtend = get16(bin.hdr + 0x26);
    if (nseg > MAX_SEGS || get16(bin.hdr + 0x22) != 64 || old_rt != 64 + nseg * 8 ||
        !(old_rtend <= get16(bin.hdr + 0x28) && get16(bin.hdr + 0x28) <= get16(bin.hdr + 0x2A) &&
          get16(bin.hdr + 0x2A) <= get16(bin.hdr + 0x04))) {
        return fail("unexpected table order in ", bin_path);
    }
    rtlen = build_restab(restab, slot_of, &name_base);
    (void)name_base;
    tail_from = old_rtend;
    tail_to = bin.hdr_bytes;
    if ((u32)old_rt + rtlen + (tail_to - tail_from) > MAX_HEADER) {
        return fail("header too large", NULL);
    }
    delta = (u16)(old_rt + rtlen - old_rtend);
    memcpy(hdr, bin.hdr, old_rt);
    memcpy(hdr + old_rt, restab, rtlen);
    memcpy(hdr + old_rt + rtlen, bin.hdr + tail_from, (size_t)(tail_to - tail_from));
    put16(hdr + 0x26, (u16)(get16(bin.hdr + 0x26) + delta));
    put16(hdr + 0x28, (u16)(get16(bin.hdr + 0x28) + delta));
    put16(hdr + 0x2A, (u16)(get16(bin.hdr + 0x2A) + delta));
    put16(hdr + 0x04, (u16)(get16(bin.hdr + 0x04) + delta));
    put32(hdr + 0x08, 0);                       /* no CRC */
    put16(hdr + 0x34, (u16)res_count);
    memcpy(segs, bin.hdr + 64, (size_t)nseg * 8);
    shift = get16(bin.hdr + 0x32);
    nonres_size = get16(bin.hdr + 0x20);
    nonres_at = bin.ne + old_rt + rtlen + (tail_to - tail_from);
    put32(hdr + 0x2C, nonres_at);

    out = fopen(out_path, "wb");
    if (out == NULL) return fail("cannot create ", out_path);
    /* MZ stub, then the header placeholder (written again at the end). */
    if (!copy_bytes(bin.f, 0, bin.ne, out)) goto done;
    if (fwrite(hdr, 1, (size_t)(nonres_at - bin.ne), out) != (size_t)(nonres_at - bin.ne)) goto done;
    if (!copy_bytes(bin.f, get32(bin.hdr + 0x2C), nonres_size, out)) goto done;
    /* Segments with their relocation records. */
    for (i = 0; i < nseg; ++i) {
        u8 *s = segs + i * 8;
        u32 from = (u32)get16(s) << shift, size = get16(s + 2) ? get16(s + 2) : 0x10000UL;
        u8 count[2];
        if (get16(s) == 0) continue;
        if (!pad_to(out, 1UL << shift)) goto done;
        pos = (u32)ftell(out);
        if ((pos >> shift) > 0xFFFFUL) { fail("driver too large", NULL); goto done; }
        put16(s, (u16)(pos >> shift));
        if (get16(s + 4) & 0x0100) {
            if (!read_at(bin.f, from + size, count, 2)) goto done;
            size += 2 + (u32)get16(count) * 8;
        }
        if (!copy_bytes(bin.f, from, size, out)) goto done;
    }
    memcpy(hdr + 64, segs, (size_t)nseg * 8);
    /* Resources. */
    for (r = 0; r < res_count; ++r) {
        FILE *src = res[r].from_vga ? vga.f : bin.f;
        u8 *slot = hdr + old_rt + slot_of[r];
        if (!pad_to(out, 1UL << RES_SHIFT)) goto done;
        pos = (u32)ftell(out);
        if ((pos >> RES_SHIFT) > 0xFFFFUL || (res[r].bytes >> RES_SHIFT) > 0xFFFFUL) {
            fail("driver too large", NULL);
            goto done;
        }
        put16(slot, (u16)(pos >> RES_SHIFT));
        put16(slot + 2, (u16)((res[r].bytes + (1UL << RES_SHIFT) - 1) >> RES_SHIFT));
        if (!copy_bytes(src, res[r].offset, res[r].bytes, out)) goto done;
    }
    if (!pad_to(out, 1UL << RES_SHIFT)) goto done;
    if (fseek(out, (long)bin.ne, SEEK_SET) != 0 ||
        fwrite(hdr, 1, (size_t)(nonres_at - bin.ne), out) != (size_t)(nonres_at - bin.ne)) {
        goto done;
    }
    ok = 1;
done:
    if (fclose(out) != 0) ok = 0;
    fclose(bin.f);
    fclose(vga.f);
    if (!ok) {
        remove(out_path);
        return fail("could not write ", out_path);
    }
    printf("S3PREP: wrote %s with %d resources from %s\n", out_path,
           res_count - 1, vga_path);
    return 1;
}

static const char *const support[] = {
    "VGA.3GR", "VGALOGO.LGO", "VGALOGO.RLE", "VGASYS.FON", "VGAFIX.FON", "VGAOEM.FON",
    "CGA40WOA.FON", "CGA80WOA.FON", "EGA40WOA.FON", "EGA80WOA.FON", "DOSAPP.FON"
};

static int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb"), *out;
    size_t n;
    int ok = 1;
    if (in == NULL) return 0;
    out = fopen(to, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) != 0) {
        if (fwrite(buf, 1, n, out) != n) ok = 0;
    }
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    return ok;
}

int main(int argc, char **argv)
{
    static char here[256], src_dir[256], win_dir[256], vga[300], bin[300], drv[300], a[300], b[300];
    unsigned i, missing = 0;
    size_t n;

    printf("S3PREP: builds S3WIDE.DRV from your own VGA.DRV\n");
    if (argc != 2) {
        printf("usage: S3PREP <WINDOWS folder or path of VGA.DRV>\n"
               "  e.g. S3PREP C:\\WINDOWS\n");
        return 2;
    }
    dir_of(argv[0], here, sizeof(here));
    n = strlen(argv[1]);
    if (n > 4 && (strcmp(argv[1] + n - 4, ".DRV") == 0 || strcmp(argv[1] + n - 4, ".drv") == 0)) {
        strncpy(vga, argv[1], sizeof(vga) - 1);
        dir_of(vga, src_dir, sizeof(src_dir));
    } else {
        strncpy(win_dir, argv[1], sizeof(win_dir) - 1);
        join(src_dir, sizeof(src_dir), argv[1], "SYSTEM");
        join(vga, sizeof(vga), src_dir, "VGA.DRV");
        if (!file_exists(vga)) {
            strncpy(src_dir, argv[1], sizeof(src_dir) - 1);
            join(vga, sizeof(vga), src_dir, "VGA.DRV");
        }
    }
    if (!file_exists(vga)) {
        printf("S3PREP: no VGA.DRV in %s or its SYSTEM folder.\n"
               "Unpack it from your Windows disks, for example\n"
               "  EXPAND A:\\VGA.DR_ C:\\VGA.DRV\n"
               "then run  S3PREP C:\\VGA.DRV\n", argv[1]);
        return 1;
    }
    join(bin, sizeof(bin), here, "S3WIDE.BIN");
    join(drv, sizeof(drv), here, "S3WIDE.DRV");
    if (!make_driver(bin, vga, drv)) return 1;

    for (i = 0; i < sizeof(support) / sizeof(support[0]); ++i) {
        join(a, sizeof(a), src_dir, support[i]);
        join(b, sizeof(b), here, support[i]);
        if (file_exists(b)) continue;
        if (!copy_file(a, b)) {
            /* Some files may live in the WINDOWS folder itself. */
            join(a, sizeof(a), win_dir, support[i]);
            if (!(win_dir[0] && copy_file(a, b))) {
                printf("S3PREP: %s not found; Setup will ask for a Windows disk with it\n",
                       support[i]);
                ++missing;
            }
        }
    }
    printf("S3PREP: done%s. Now install with Windows Setup (see INSTALL.TXT).\n",
           missing ? "" : ", all Setup files are here");
    return 0;
}
