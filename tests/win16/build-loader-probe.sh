#!/bin/sh
set -eu
cd "$(dirname "$0")"

if [ -z "${WATCOM:-}" ]; then
    echo "set WATCOM to an Open Watcom 2 installation" >&2
    exit 2
fi
OWBIN=${OWBIN:-binl64}
if [ ! -x "$WATCOM/$OWBIN/wcl" ]; then
    echo "missing executable: $WATCOM/$OWBIN/wcl" >&2
    exit 2
fi
export PATH="$WATCOM/$OWBIN:$PATH"
export INCLUDE="$WATCOM/h${INCLUDE:+:$INCLUDE}"
mkdir -p ../../build
wcl -q -zW -bt=windows -ml -i="$WATCOM/h/win" \
    -fe=../../build/S3PROBE.EXE s3_loader_probe.c
echo "Built build/S3PROBE.EXE (not executed in Windows by this script)."
