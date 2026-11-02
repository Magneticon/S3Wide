#!/bin/sh
# Assemble the test package from a local build:
#   output/release/S3WIDE-<date>/  S3WIDE.DRV, OEMSETUP.INF, VBEMODES.EXE,
#                                  INSTALL.TXT, SHA256.TXT (DOS line ends)
#   output/release/S3WIDE-<date>.zip
# The driver must be a build with VGA_DRV resources (see build.sh); such a
# package contains Microsoft resources and stays local (output/ is ignored).
#
# Usage: package/make-package.sh [DRIVER] [DATE]
#   DRIVER  default build/s3wide_driver.dll
#   DATE    default today (YYYYMMDD)
# Environment: WATCOM, OWBIN (to build VBEMODES.EXE), PYTHON (default python3)
set -eu
repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
driver=${1:-build/s3wide_driver.dll}
date=${2:-$(date +%Y%m%d)}
PYTHON=${PYTHON:-python3}
OWBIN=${OWBIN:-binl64}
out=output/release/S3WIDE-$date

for path in "$driver" package/OEMSETUP.INF package/INSTALL.TXT; do
    [ -f "$path" ] || { echo "missing: $path" >&2; exit 2; }
done
mkdir -p build
PATH="$WATCOM/$OWBIN:$PATH" INCLUDE="$WATCOM/h" \
    wcl -q -bt=dos -ms -fo=build/vbe_modes.obj -fe=build/VBEMODES.EXE \
    tests/dosbox-x/vbe_modes.c

rm -rf "$out" "$out.zip"
mkdir -p "$out"
cp "$driver" "$out/S3WIDE.DRV"
cp build/VBEMODES.EXE "$out/VBEMODES.EXE"
"$PYTHON" - "$out" <<'EOF'
import hashlib, os, sys, zipfile
out = sys.argv[1]
for name in ("OEMSETUP.INF", "INSTALL.TXT"):
    text = open(os.path.join("package", name), newline="").read()
    text = text.replace("\r\n", "\n").replace("\n", "\r\n")
    open(os.path.join(out, name), "w", newline="").write(text)
files = ["S3WIDE.DRV", "OEMSETUP.INF", "VBEMODES.EXE", "INSTALL.TXT"]
with open(os.path.join(out, "SHA256.TXT"), "w", newline="") as sums:
    for name in files:
        digest = hashlib.sha256(open(os.path.join(out, name), "rb").read()).hexdigest()
        sums.write("%s *%s\r\n" % (digest, name))
with zipfile.ZipFile(out + ".zip", "w", zipfile.ZIP_DEFLATED) as z:
    for name in files + ["SHA256.TXT"]:
        z.write(os.path.join(out, name), name)
print(out + ".zip")
EOF
