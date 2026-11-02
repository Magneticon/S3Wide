#!/bin/sh
# Build the redistributable release from source, with no Microsoft content:
#   output/release/S3WIDE-<version>/  S3WIDE.BIN  driver without the VGA.DRV
#                                                 resources (S3PREP adds them
#                                                 from the user's own VGA.DRV)
#                                     S3PREP.EXE  DOS tool that makes S3WIDE.DRV
#                                     OEMSETUP.INF, DISPDIB.DLL, VBEMODES.EXE,
#                                     INSTALL.TXT, S3WIDE-GUIDE.PDF (if built),
#                                     SHA256.TXT
#   output/release/S3WIDE-<version>.zip
#
# Usage: package/make-release.sh VERSION
# Environment: WATCOM, OWBIN, CC (host compiler for the driver's host tests),
#   PYTHON (default python3).
set -eu
repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
version=${1:?usage: make-release.sh VERSION}
PYTHON=${PYTHON:-python3}
OWBIN=${OWBIN:-binl64}
out=output/release/S3WIDE-$version

# The driver exactly as the sources make it: no VGA_DRV, no trace.
env -u VGA_DRV S3_TRACE=0 sh ./build.sh > build/release-build.log
"$PYTHON" - build/s3wide_driver.dll <<'EOF'
import struct, sys
d = open(sys.argv[1], "rb").read()
ne = struct.unpack_from("<I", d, 0x3C)[0]
rt, end = struct.unpack_from("<HH", d, ne + 0x24)
types = []
p = ne + rt + 2
while struct.unpack_from("<H", d, p)[0]:
    t, n = struct.unpack_from("<HH", d, p)
    types.append((t, n))
    p += 8 + 12 * n
if types != [(0x8010, 1)]:
    sys.exit("driver carries resources other than its version: %r" % types)
EOF
PATH="$WATCOM/$OWBIN:$PATH" INCLUDE="$WATCOM/h" \
    wcl -q -bt=dos -ml -fo=build/s3prep.obj -fe=build/S3PREP.EXE tools/s3prep/s3prep.c
PATH="$WATCOM/$OWBIN:$PATH" INCLUDE="$WATCOM/h" \
    wcl -q -bt=dos -ms -fo=build/vbe_modes.obj -fe=build/VBEMODES.EXE tests/dosbox-x/vbe_modes.c
WATCOM=$WATCOM OWBIN=$OWBIN sh dispdib/build.sh > /dev/null

rm -rf "$out" "$out.zip"
mkdir -p "$out"
cp build/s3wide_driver.dll "$out/S3WIDE.BIN"
cp build/S3PREP.EXE build/VBEMODES.EXE build/DISPDIB.DLL "$out/"
extra=
if [ -f docs/guide/S3WIDE-GUIDE.pdf ]; then
    cp docs/guide/S3WIDE-GUIDE.pdf "$out/S3WIDE-GUIDE.PDF"
    extra=S3WIDE-GUIDE.PDF
fi
"$PYTHON" - "$out" $extra <<'EOF'
import hashlib, os, sys, zipfile
out = sys.argv[1]
for name in ("OEMSETUP.INF", "INSTALL.TXT"):
    text = open(os.path.join("package", name), newline="").read()
    text = text.replace("\r\n", "\n").replace("\n", "\r\n")
    open(os.path.join(out, name), "w", newline="").write(text)
files = ["S3WIDE.BIN", "S3PREP.EXE", "OEMSETUP.INF", "DISPDIB.DLL", "VBEMODES.EXE",
         "INSTALL.TXT"] + sys.argv[2:]
with open(os.path.join(out, "SHA256.TXT"), "w", newline="") as sums:
    for name in files:
        digest = hashlib.sha256(open(os.path.join(out, name), "rb").read()).hexdigest()
        sums.write("%s *%s\r\n" % (digest, name))
with zipfile.ZipFile(out + ".zip", "w", zipfile.ZIP_DEFLATED) as z:
    for name in files + ["SHA256.TXT"]:
        z.write(os.path.join(out, name), name)
print(out + ".zip")
EOF
