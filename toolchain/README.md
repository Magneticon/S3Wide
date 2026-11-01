# Build toolchain provenance

* Distribution: Open Watcom V2 2.0 C/C++ Linux x86_64 package, official GitHub
  `Current-build` release.
* URL: `https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/open-watcom-2_0-c-linux-x64`
* Downloaded in this execution on 2026-10-09.
* SHA-256: `cd922f6200a20505cdda44dc0155471ae3d3f53e33a354097f3d89de74dc49c6`
* The package's embedded `license.txt` is the Sybase
  Open Watcom Public License, version 1.0. Read and comply with its terms before
  use/deployment. This package is not represented as OSI-approved or as
  unrestricted public domain software.

The 129 MB toolchain package itself is not copied into this recovery project;
download it from the URL above and verify the hash before extraction. The Linux
package is a self-extracting executable containing a ZIP archive; when the
installer cannot run in a headless environment, extract the embedded archive
with a ZIP reader and restore executable permission to the `binl64` tools.
