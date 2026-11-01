#!/bin/sh
# Boot a disposable copy of a Windows 3.1x guest under DOSBox-X with
# DISPBASE.EXE, then verify the source guest was not modified.
#
# Usage: run-display-baseline.sh RUN_DIR
# Environment:
#   GUEST_SRC  directory-mounted guest to copy (default /c/DOS); never mounted
#   DOSBOX_X   DOSBox-X executable (default /c/DOSBox-X/dosbox-x_x64_SDL2.exe)
#   PROBE      built probe, run as D:\<name> (default build/DISPBASE.EXE)
#   OVERLAY    optional directory copied onto the guest copy (for example
#              S3TEST/S3WIDE.DRV or a modified WINDOWS/SYSTEM.INI)
#   MACHINE    DOSBox-X machine type (default svga_s3trio64)
#   VMEMSIZE   video memory in MiB (default 4)
#   TIME_LIMIT emulator wall-clock limit in seconds (default 150)
#   VISIBLE    1 = open a real emulator window and capture it from the host
#              with capture-window.ps1 (DISPBASE.EXE only); default headless
#   HOLD       seconds DISPBASE.EXE keeps the desktop up for that capture
#              (default 6 when VISIBLE=1)
#   FALLBACK   with VISIBLE=1, capture anyway after this many seconds if the
#              probe never signals (default 0 = wait for the probe)
#   WIN_ARGS   extra WIN.COM switches, for example /b for BOOTLOG.TXT
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
OVERLAY=${OVERLAY:-}
probe_name=$(basename "$PROBE")
MACHINE=${MACHINE:-svga_s3trio64}
VMEMSIZE=${VMEMSIZE:-4}
TIME_LIMIT=${TIME_LIMIT:-150}
VISIBLE=${VISIBLE:-0}
FALLBACK=${FALLBACK:-0}
WIN_ARGS=${WIN_ARGS:-}
if [ "$VISIBLE" = 1 ]; then
    HOLD=${HOLD:-6}
    probe_args=" $HOLD"
else
    probe_args=
fi

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
manifest "$run/guest" > "$run/guest.copy.sha256"
cmp -s "$run/source.before.sha256" "$run/guest.copy.sha256" || {
    echo "guest copy does not match source" >&2
    exit 1
}
if [ -n "$OVERLAY" ]; then
    cp -rp "$OVERLAY"/. "$run/guest/"
    manifest "$OVERLAY" > "$run/overlay.sha256"
fi
cp -p "$PROBE" "$run/hostd/$probe_name"
manifest "$run/guest" > "$run/guest.before.sha256"

win_run=$(cygpath -w "$run")
cat > "$run/baseline.conf" <<EOF
[sdl]
fullscreen=false
output=surface
windowresolution=original
# Keep the whole client area on screen: off-screen parts of the window are
# never painted and would capture as black.
windowposition=0,0

[dosbox]
title=S3 display baseline
machine=$MACHINE
memsize=32
quit warning=false
dpi aware=true
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
win ${WIN_ARGS:+$WIN_ARGS }D:\\$probe_name$probe_args
exit
EOF

status=0
capture_pid=
if [ "$VISIBLE" = 1 ]; then
    powershell.exe -NoProfile -ExecutionPolicy Bypass \
        -File "$(cygpath -w "$repo/tests/win16/capture-window.ps1")" \
        -ReadyFile "$win_run\\hostd\\OUTPUT\\READY.TXT" \
        -OutFile "$win_run\\emulator-window.png" \
        -TimeoutSec "$TIME_LIMIT" -FallbackSec "$FALLBACK" \
        > "$run/capture.txt" 2>&1 &
    capture_pid=$!
    (cd "$run" && SDL_AUDIODRIVER=dummy "$DOSBOX_X" -nomenu \
        -conf "$win_run\\baseline.conf" -time-limit "$TIME_LIMIT" \
        > stdout.txt 2> stderr.txt) || status=$?
else
    (cd "$run" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        "$DOSBOX_X" -conf "$win_run\\baseline.conf" -time-limit "$TIME_LIMIT" \
        > stdout.txt 2> stderr.txt) || status=$?
fi
echo "dosbox_exit=$status"
if [ -n "$capture_pid" ]; then
    wait "$capture_pid" || true
    cat "$run/capture.txt"
fi

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
