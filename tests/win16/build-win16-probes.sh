#!/bin/sh
# Build the autonomous Win16 probes used by run-display-baseline.sh:
#   build/DISPBASE.EXE  display baseline and GDI screen readback
#   build/S3AUTO.EXE    S3WIDE.DRV loader probe (no message boxes)
#   build/DISPBNCH.EXE  GDI timing benchmark on the screen DC
#   build/DISPRLE.EXE   RLE8/RLE4 DIBs drawn against uncompressed ones
#   build/DISPHUGE.EXE  huge bitmaps: GDI layout, BitBlt and StretchBlt rows
#   build/DIBROWS.EXE   SetDIBits/GetDIBits row placement (compare VGA.DRV)
set -eu
cd "$(dirname "$0")"

if [ -z "${WATCOM:-}" ]; then
    echo "set WATCOM to an Open Watcom 2 installation" >&2
    exit 2
fi
OWBIN=${OWBIN:-binl64}
case "$OWBIN" in
    binnt*|binw) incsep=';' ;;
    *) incsep=':' ;;
esac
if [ ! -x "$WATCOM/$OWBIN/wcl" ]; then
    echo "missing executable: $WATCOM/$OWBIN/wcl" >&2
    exit 2
fi
export PATH="$WATCOM/$OWBIN:$PATH"
export INCLUDE="$WATCOM/h${INCLUDE:+$incsep$INCLUDE}"
mkdir -p ../../build
wcl -q -zW -bt=windows -ml -i="$WATCOM/h/win" \
    -fo=../../build/display_baseline_probe.obj \
    -fe=../../build/DISPBASE.EXE display_baseline_probe.c
wcl -q -zW -bt=windows -ml -i="$WATCOM/h/win" \
    -fo=../../build/s3_loader_probe_autonomous.obj \
    -fe=../../build/S3AUTO.EXE s3_loader_probe_autonomous.c
wcl -q -zW -bt=windows -ml -i="$WATCOM/h/win" \
    -fo=../../build/display_bench_probe.obj \
    -fe=../../build/DISPBNCH.EXE display_bench_probe.c
wcl -q -zW -bt=windows -ml -i="$WATCOM/h/win" \
    -fo=../../build/display_rle_probe.obj \
    -fe=../../build/DISPRLE.EXE display_rle_probe.c
wcl -q -zW -bt=windows -ml -i="$WATCOM/h/win" \
    -fo=../../build/display_huge_probe.obj \
    -fe=../../build/DISPHUGE.EXE display_huge_probe.c
wcl -q -zW -bt=windows -ml -i="$WATCOM/h/win" \
    -fo=../../build/display_dibrows_probe.obj \
    -fe=../../build/DIBROWS.EXE display_dibrows_probe.c
echo "Built the probes above in build/ (not executed by this script)."
