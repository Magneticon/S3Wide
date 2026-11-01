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
drvflags="-q -bt=windows -mc -zu -zc -s -i=include"
# S3_TRACE=1 builds a bring-up driver that logs DDA calls to C:\S3TRACE.TXT.
if [ "${S3_TRACE:-0}" = 1 ]; then
    drvflags="$drvflags -dS3_TRACE"
fi
mkdir -p build
rm -f build/*.obj build/driver_smoke.res build/s3wide_driver.dll build/host_checks

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
wrc -q -r -bt=windows -fo=build/driver_smoke.res src/driver_smoke.rc
cp "$WATCOM/lib286/win/libentry.obj" build/libentry.obj
wlink @src/driver_smoke.lnk
"$PYTHON" inspect_ne.py build/s3wide_driver.dll build/driver_smoke.map > build/inspection.txt
cat build/inspection.txt
# The host compiler must not see Watcom's headers (clang on Windows reads INCLUDE).
env -u INCLUDE "$CC" -std=c89 -Wall -Wextra -Werror -DS3_HOST_TEST -Iinclude \
    src/s3_gdiinfo.c src/s3_color.c src/s3_surface.c src/s3_exports.c \
    src/s3_gdi_stubs.c \
    src/s3_hw.c src/s3_vga_state.c src/s3_win16_bridge.c src/s3_device.c tests/host_test.c \
    -o build/host_checks
./build/host_checks
