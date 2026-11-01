#!/bin/sh
set -eu

if [ -z "${WATCOM:-}" ]; then
    echo "set WATCOM to the root of an Open Watcom installation" >&2
    exit 2
fi
OWBIN=${OWBIN:-binl64}
for tool in wcc wlink wrc; do
    if [ ! -x "$WATCOM/$OWBIN/$tool" ]; then
        echo "missing executable: $WATCOM/$OWBIN/$tool" >&2
        exit 2
    fi
done

export PATH="$WATCOM/$OWBIN:$PATH"
export INCLUDE="$WATCOM/h${INCLUDE:+:$INCLUDE}"
mkdir -p build
rm -f build/*.obj build/driver_smoke.res build/s3wide_driver.dll build/host_checks

wcc -q -bt=windows -mc -zu -zc -i=include -fo=build/s3_gdiinfo.obj src/s3_gdiinfo.c
wcc -q -bt=windows -mc -zu -zc -i=include -fo=build/s3_color.obj src/s3_color.c
wcc -q -bt=windows -mc -zu -zc -i=include -fo=build/s3_surface.obj src/s3_surface.c
wcc -q -bt=windows -mc -zu -zc -i=include -fo=build/s3_hw.obj src/s3_hw.c
wcc -q -bt=windows -mc -zu -zc -i=include -fo=build/s3_vga_state.obj src/s3_vga_state.c
wcc -q -bt=windows -mc -zu -zc -i=include -fo=build/s3_win16_bridge.obj src/s3_win16_bridge.c
wcc -q -bt=windows -mc -zu -zc -i=include -fo=build/s3_win16_native.obj src/s3_win16_native.c
wcc -q -bt=windows -mc -zu -zc -i=include -fo=build/s3_device.obj src/s3_device.c
wcc -q -bt=windows -mc -zu -zc -i=include -fo=build/s3_exports.obj src/s3_exports.c
wcc -q -bt=windows -mc -zu -zc -i=include -fo=build/s3_gdi_stubs.obj src/s3_gdi_stubs.c
wrc -q -r -bt=windows -fo=build/driver_smoke.res src/driver_smoke.rc
cp "$WATCOM/lib286/win/libentry.obj" build/libentry.obj
wlink @src/driver_smoke.lnk
python3 inspect_ne.py build/s3wide_driver.dll build/driver_smoke.map > build/inspection.txt
cat build/inspection.txt
gcc -std=c89 -Wall -Wextra -Werror -DS3_HOST_TEST -Iinclude \
    src/s3_gdiinfo.c src/s3_color.c src/s3_surface.c src/s3_exports.c \
    src/s3_gdi_stubs.c \
    src/s3_hw.c src/s3_vga_state.c src/s3_win16_bridge.c src/s3_device.c tests/host_test.c \
    -o build/host_checks
./build/host_checks
