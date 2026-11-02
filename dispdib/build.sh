#!/bin/sh
# Build build/DISPDIB.DLL, the widescreen replacement for Video for Windows'
# full-screen display DLL. Run from the repository root:
#   WATCOM=... OWBIN=binnt64 sh dispdib/build.sh
set -eu
if [ -z "${WATCOM:-}" ]; then
    echo "set WATCOM to the root of an Open Watcom installation" >&2
    exit 2
fi
OWBIN=${OWBIN:-binl64}
case "$OWBIN" in
    binnt*|binw) incsep=';' ;;
    *) incsep=':' ;;
esac
export PATH="$WATCOM/$OWBIN:$PATH"
export INCLUDE="$WATCOM/h$incsep$WATCOM/h/win${INCLUDE:+$incsep$INCLUDE}"
mkdir -p build
# Large model, SS != DS (callers' stacks), 386 code, no stack probes.
wcc -q -bt=windows -ml -zu -s -3 -ox -ot -fo=build/dispdib.obj dispdib/dispdib.c
cp "$WATCOM/lib286/win/libentry.obj" build/dd_libentry.obj
wlink @dispdib/dispdib.lnk
ls -l build/DISPDIB.DLL
