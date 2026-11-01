#!/bin/sh
# Boot a disposable copy of a Windows 3.1x guest under DOSBox-X with
# DISPBASE.EXE, then verify the source guest was not modified.
#
# Usage: run-display-baseline.sh RUN_DIR
# Environment:
#   GUEST_SRC  directory-mounted guest to copy (default /c/DOS); never mounted
#   DOSBOX_X   DOSBox-X executable (default /c/DOSBox-X/dosbox-x_x64_SDL2.exe)
#   PROBE      built probe (default build/DISPBASE.EXE)
#   MACHINE    DOSBox-X machine type (default svga_s3trio64)
#   VMEMSIZE   video memory in MiB (default 4)
#   TIME_LIMIT emulator wall-clock limit in seconds (default 150)
# Requires Git Bash or MSYS2 on Windows (uses cygpath).
set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 RUN_DIR" >&2
    exit 2
fi
repo=$(cd "$(dirname "$0")/../.." && pwd)
run=$1
GUEST_SRC=${GUEST_SRC:-/c/DOS}
DOSBOX_X=${DOSBOX_X:-/c/DOSBox-X/dosbox-x_x64_SDL2.exe}
PROBE=${PROBE:-$repo/build/DISPBASE.EXE}
MACHINE=${MACHINE:-svga_s3trio64}
VMEMSIZE=${VMEMSIZE:-4}
TIME_LIMIT=${TIME_LIMIT:-150}

if [ -e "$run" ]; then
    echo "refusing to reuse existing run directory: $run" >&2
    exit 2
fi
for path in "$GUEST_SRC/WINDOWS/WIN.COM" "$DOSBOX_X" "$PROBE"; do
    if [ ! -f "$path" ]; then
        echo "missing: $path" >&2
        exit 2
    fi
done

manifest() {
    (cd "$1" && find . -type f -print0 | sort -z | xargs -0 sha256sum)
}

mkdir -p "$run/hostd/OUTPUT" "$run/capture"
run=$(cd "$run" && pwd)
manifest "$GUEST_SRC" > "$run/source.before.sha256"
cp -rp "$GUEST_SRC" "$run/guest"
manifest "$run/guest" > "$run/guest.before.sha256"
cmp -s "$run/source.before.sha256" "$run/guest.before.sha256" || {
    echo "guest copy does not match source" >&2
    exit 1
}
cp -p "$PROBE" "$run/hostd/DISPBASE.EXE"

win_run=$(cygpath -w "$run")
cat > "$run/baseline.conf" <<EOF
[sdl]
fullscreen=false
output=surface
windowresolution=original

[dosbox]
title=S3 display baseline
machine=$MACHINE
memsize=32
quit warning=false
working directory option=config
integration device=false
captures=$win_run\\capture

[video]
vmemsize=$VMEMSIZE

[cpu]
core=normal
cputype=pentium
cycles=max

[render]
aspect=false
scaler=none

[log]
logfile=$win_run\\dosbox.log

[autoexec]
mount C: $win_run\\guest
mount D: $win_run\\hostd
C:
cd \\WINDOWS
win D:\\DISPBASE.EXE
exit
EOF

status=0
(cd "$run" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$DOSBOX_X" -conf "$win_run\\baseline.conf" -time-limit "$TIME_LIMIT" \
    > stdout.txt 2> stderr.txt) || status=$?
echo "dosbox_exit=$status"

manifest "$GUEST_SRC" > "$run/source.after.sha256"
if cmp -s "$run/source.before.sha256" "$run/source.after.sha256"; then
    echo "source_guest=unchanged"
else
    echo "source_guest=CHANGED" >&2
    status=1
fi
manifest "$run/guest" > "$run/guest.after.sha256"

if [ -f "$run/hostd/OUTPUT/RESULT.TXT" ]; then
    cat "$run/hostd/OUTPUT/RESULT.TXT"
else
    echo "no probe result" >&2
    status=1
fi
if [ -f "$run/hostd/OUTPUT/SCREEN.BMP" ]; then
    python -I "$repo/tests/win16/bmp24_to_png.py" \
        "$run/hostd/OUTPUT/SCREEN.BMP" "$run/screen.png"
fi
exit $status
