#!/bin/sh
set -eu
if [ -z "${WATCOM:-}" ]; then echo "set WATCOM to an Open Watcom installation" >&2; exit 2; fi
OWBIN=${OWBIN:-binl64}
PATH="$WATCOM/$OWBIN:$PATH"
INCLUDE="$WATCOM/h${INCLUDE:+:$INCLUDE}"
export PATH INCLUDE
rm -f S3RAW.EXE S3RAW.obj s3_hw.obj
wcc -q -bt=dos -ms -I../../include -fo=S3RAW.obj raw_s3_smoke.c
wcc -q -bt=dos -ms -I../../include -fo=s3_hw.obj ../../src/s3_hw.c
wlink system dos option quiet name S3RAW.EXE file S3RAW.obj file s3_hw.obj
rm -f S3RAW.obj s3_hw.obj
