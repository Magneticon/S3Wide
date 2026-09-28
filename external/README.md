# Local/external dependencies

This directory is for dependencies that should not automatically be committed.

Suggested local layout:

```
external/
  win31ddk/
  msvc/
  s3-original/
  s3-docs/
  dosbox-x/
  vbesvga/
```

## Required/expected

### Win16 DDK
Provide a legally obtained Windows 3.1/Win16 DDK tree. The build scripts should discover its `286\TOOLS` and `286\INC` directories.

### Contemporary Visual C++
The exact version will be pinned after reproducing a known-good build. Do not commit Microsoft compiler binaries.

### EXE2BIN
A compatible EXE2BIN is needed by the reference VBESVGA build procedure. A redistributable implementation may be sourced separately where licensing permits.

### Original S3 driver
Place the Windows 3.1 S3 Trio 1.70.04 package under `s3-original/` for behavioral comparison and local analysis. Do not commit it unless redistribution rights are confirmed.

### S3 documentation
Place locally obtained Trio64 programming references under `s3-docs/`. Record provenance/licensing before committing any document.

### Open-source references
DOSBox-X and VBESVGA may be cloned locally for research. Prefer links/submodules only after license and repository strategy are deliberately chosen.
