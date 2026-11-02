#!/bin/sh
set -eu

if [ -z "${WATCOM:-}" ]; then
    echo "set WATCOM to the root of an Open Watcom installation" >&2
    exit 2
fi
OWBIN=${OWBIN:-binl64}
CC=${CC:-gcc}
PYTHON=${PYTHON:-python3}
# Windows-hosted Watcom tools (binnt, binnt64, binw) read ';'-separated INCLUDE.
case "$OWBIN" in
    binnt*|binw) incsep=';' ;;
    *) incsep=':' ;;
esac
for tool in wcc wlink wrc; do
    if [ ! -x "$WATCOM/$OWBIN/$tool" ]; then
        echo "missing executable: $WATCOM/$OWBIN/$tool" >&2
        exit 2
    fi
done

export PATH="$WATCOM/$OWBIN:$PATH"
export INCLUDE="$WATCOM/h${INCLUDE:+$incsep$INCLUDE}"
# -zu: SS != DS (the driver runs on the caller's stack). -s: no stack probes;
# Watcom's __STK assumes the module's own stack and its overflow path calls
# MessageBox, which imports USER. Display drivers load before USER exists,
# so any USER import makes Windows fail to load the driver.
# -3: 386 instructions (Enhanced Mode and the DPMI frame buffer need a 386
# anyway); 32-bit arithmetic then uses 32-bit registers, which is why the
# interrupt-time cursor entries save them (s3_dda_entries.c). -zfp -zgp: never
# use FS/GS. -ox -ot: full optimization for speed.
drvflags="-q -bt=windows -mc -zu -zc -s -3 -zfp -zgp -ox -ot -i=include"
# S3_TRACE=1 builds a bring-up driver that logs DDA calls to C:\S3TRACE.TXT.
if [ "${S3_TRACE:-0}" = 1 ]; then
    drvflags="$drvflags -dS3_TRACE"
fi
mkdir -p build
rm -f build/*.obj build/*.res build/s3wide_driver.dll build/host_checks

wcc $drvflags -fo=build/s3_gdiinfo.obj src/s3_gdiinfo.c
wcc $drvflags -fo=build/s3_color.obj src/s3_color.c
wcc $drvflags -fo=build/s3_surface.obj src/s3_surface.c
wcc $drvflags -fo=build/s3_hw.obj src/s3_hw.c
wcc $drvflags -fo=build/s3_vga_state.obj src/s3_vga_state.c
wcc $drvflags -fo=build/s3_win16_bridge.obj src/s3_win16_bridge.c
wcc $drvflags -fo=build/s3_win16_native.obj src/s3_win16_native.c
wcc $drvflags -fo=build/s3_device.obj src/s3_device.c
wcc $drvflags -fo=build/s3_exports.obj src/s3_exports.c
wcc $drvflags -fo=build/s3_gdi_stubs.obj src/s3_gdi_stubs.c
wcc $drvflags -fo=build/s3_trace.obj src/s3_trace.c
wcc $drvflags -fo=build/s3_dda_entries.obj src/s3_dda_entries.c
wcc $drvflags -fo=build/s3_win16_hw.obj src/s3_win16_hw.c
wcc $drvflags -fo=build/s3_brush.obj src/s3_brush.c
wcc $drvflags -fo=build/s3_raster.obj src/s3_raster.c
wcc $drvflags -fo=build/s3_dib.obj src/s3_dib.c
wcc $drvflags -fo=build/s3_text.obj src/s3_text.c
wcc $drvflags -fo=build/s3_output.obj src/s3_output.c
wcc $drvflags -fo=build/s3_cursor.obj src/s3_cursor.c
wcc $drvflags -fo=build/s3_mode.obj src/s3_mode.c
wcc $drvflags -fo=build/s3_vbe.obj src/s3_vbe.c
wcc $drvflags -fo=build/s3_swcursor.obj src/s3_swcursor.c
wcc $drvflags -fo=build/s3_copy.obj src/s3_copy.c
wrc -q -r -zm -bt=windows -fo=build/driver_smoke.res src/driver_smoke.rc
# VGA_DRV=path/to/licensed/VGA.DRV: LOCAL bring-up build that also carries
# that driver's cursors, icons, OEM bitmaps and OEMBIN tables. The result
# contains Microsoft resources and must not be committed or distributed.
inspect_flags=
if [ -n "${VGA_DRV:-}" ]; then
    "$PYTHON" tools/import_vga_resources.py "$VGA_DRV" build/vga_local.res
    cat build/driver_smoke.res build/vga_local.res > build/driver.res
    inspect_flags=--allow-imported-resources
else
    cp build/driver_smoke.res build/driver.res
fi
cp "$WATCOM/lib286/win/libentry.obj" build/libentry.obj
wlink @src/driver_smoke.lnk
"$PYTHON" inspect_ne.py $inspect_flags build/s3wide_driver.dll build/driver_smoke.map > build/inspection.txt
cat build/inspection.txt
# The host compiler must not see Watcom's headers (clang on Windows reads INCLUDE).
env -u INCLUDE "$CC" -std=c89 -Wall -Wextra -Werror -DS3_HOST_TEST -Iinclude \
    src/s3_gdiinfo.c src/s3_color.c src/s3_surface.c src/s3_exports.c \
    src/s3_gdi_stubs.c src/s3_brush.c src/s3_raster.c src/s3_dib.c src/s3_text.c src/s3_output.c src/s3_cursor.c \
    src/s3_mode.c src/s3_vbe.c src/s3_swcursor.c src/s3_copy.c \
    src/s3_hw.c src/s3_vga_state.c src/s3_win16_bridge.c src/s3_device.c tests/host_test.c \
    -o build/host_checks
./build/host_checks
