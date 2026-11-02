#!/bin/sh
# Assemble the test package from a local build:
#   output/release/S3WIDE-<date>/  S3WIDE.DRV, OEMSETUP.INF, VBEMODES.EXE,
#                                  DISPDIB.DLL, INSTALL.TXT, SHA256.TXT (DOS line ends),
#                                  and with WINSYS the files Setup copies
#   output/release/S3WIDE-<date>.zip
# The driver must be a build with VGA_DRV resources (see build.sh); such a
# package contains Microsoft resources and stays local (output/ is ignored).
#
# Usage: package/make-package.sh [DRIVER] [DATE]
#   DRIVER  default build/s3wide_driver.dll
#   DATE    default today (YYYYMMDD)
# Environment: WATCOM, OWBIN (to build VBEMODES.EXE), PYTHON (default python3),
#   WINSYS  an installed Windows 3.1x WINDOWS\SYSTEM folder to copy the files
#           Setup asks for (VGA grabber, logo and fonts); like the driver's
#           resources these are Microsoft files, so the package stays local
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
WATCOM=$WATCOM OWBIN=$OWBIN sh dispdib/build.sh >/dev/null

rm -rf "$out" "$out.zip"
mkdir -p "$out"
cp "$driver" "$out/S3WIDE.DRV"
cp build/VBEMODES.EXE "$out/VBEMODES.EXE"
cp build/DISPDIB.DLL "$out/DISPDIB.DLL"
support="VGA.3GR VGALOGO.LGO VGALOGO.RLE VGASYS.FON VGAFIX.FON VGAOEM.FON
CGA40WOA.FON CGA80WOA.FON EGA40WOA.FON EGA80WOA.FON DOSAPP.FON"
if [ -n "${WINSYS:-}" ]; then
    for name in $support; do
        [ -f "$WINSYS/$name" ] || { echo "missing: $WINSYS/$name" >&2; exit 2; }
        cp "$WINSYS/$name" "$out/$name"
    done
else
    echo "WINSYS not set: Setup will ask for the Windows disk with $support" >&2
    support=
fi
"$PYTHON" - "$out" $support <<'EOF'
import hashlib, os, sys, zipfile
out = sys.argv[1]
for name in ("OEMSETUP.INF", "INSTALL.TXT"):
    text = open(os.path.join("package", name), newline="").read()
    text = text.replace("\r\n", "\n").replace("\n", "\r\n")
    open(os.path.join(out, name), "w", newline="").write(text)
files = ["S3WIDE.DRV", "OEMSETUP.INF", "VBEMODES.EXE", "DISPDIB.DLL", "INSTALL.TXT"] + sys.argv[2:]
with open(os.path.join(out, "SHA256.TXT"), "w", newline="") as sums:
    for name in files:
        digest = hashlib.sha256(open(os.path.join(out, name), "rb").read()).hexdigest()
        sums.write("%s *%s\r\n" % (digest, name))
with zipfile.ZipFile(out + ".zip", "w", zipfile.ZIP_DEFLATED) as z:
    for name in files + ["SHA256.TXT"]:
        z.write(os.path.join(out, name), name)
print(out + ".zip")
EOF
