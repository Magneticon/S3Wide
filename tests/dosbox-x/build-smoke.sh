#!/bin/sh
set -eu

if [ -z "${WATCOM:-}" ]; then
    echo "set WATCOM to the root of an Open Watcom installation" >&2
    exit 2
fi
OWBIN=${OWBIN:-binl64}
for tool in wcc wlink; do
    if [ ! -x "$WATCOM/$OWBIN/$tool" ]; then
        echo "missing executable: $WATCOM/$OWBIN/$tool" >&2
        exit 2
    fi
done
PATH="$WATCOM/$OWBIN:$PATH"
INCLUDE="$WATCOM/h${INCLUDE:+:$INCLUDE}"
export PATH INCLUDE
mkdir -p capture
rm -f VBECHK.EXE VBECHK.obj
wcc -q -bt=dos -ms -fo=VBECHK.obj vbe_smoke.c
wlink system dos option quiet name VBECHK.EXE file VBECHK.obj
rm -f VBECHK.obj
