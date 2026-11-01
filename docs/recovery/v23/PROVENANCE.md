# S3Win16Recovery-v23 provenance

Recovered 1 November 2026 from a user-supplied copy of the CODEX machine's
historical task folders, placed at
`C:\CLAUDE\S3\2026-10-09\task-2\S3Win16Recovery-v23`. That copy was not
fetched from the CODEX machine by this session.

This is a source snapshot from 9 October 2026. It is **not** the latest
recovery package (`S3Win16Recovery-20261012-font-layout-review.zip`, Library
v70) and does not contain the work reported for 10-19 October: packed-24
surface, truecolor objects, text/font/cursor/DIB4 suites, the standard-mode
pilot, the font-layout fix or the October 19 import preflight.

## Identity

- Stated base commit: `2db39c9` (`recovery/openwatcom-win16-abi`). That commit
  and branch are not present on the `TheKernelDoc/S3` remote.
- `src/` and `include/` are byte-identical to the sibling
  `S3Win16Recovery-v22` folder.
- All 125 entries in the bundle's own `evidence/RECOVERY-SHA256SUMS.txt`
  verified against the copied folder before staging.
- The original archive `C:\CODEX\S3Win16Recovery-20261009-v23.zip` has
  SHA-256 `9c4680c6db77c6f6365dcc5d16b824ceafc7bffeba8891a91eaa61578069268d`;
  all 126 members are byte-identical to the copied folder.
  `guest-session-status.md` and `loader-probe-run-report.md` are not in the
  archive; their timestamps (17:31, 17:18) are later than the archive (17:12).
- The bundle's built `build/s3wide_driver.dll` has SHA-256
  `3902af42d12e36713914081e9f0ef193c86b1df8768e847930f662bf150c49fe`, the
  9 October driver candidate named in `docs/RECOVERY_HANDOFF_20261101.md`.
  The binary itself is not staged.

## Layout

Files keep their original bytes and relative paths, except the bundle's
top-level reports, which are moved here to avoid replacing the repository's
own `README.md`:

| Bundle path | Repository path |
|---|---|
| `README.md`, `README_COMMIT.txt`, `guest-session-status.md`, `loader-probe-run-report.md` | `docs/recovery/v23/` |
| everything else staged | same path at the repository root |

Relative links inside the moved `README.md` therefore point one level off.

`SHA256SUMS.txt` lists every staged file with its source path. Files were
copied with `core.autocrlf=false` so the committed blobs equal the source
bytes.

## Not staged

Hashes are kept in `EXCLUDED-SHA256SUMS.txt`.

- `build/`: objects, map, resource, host test binary, `S3PROBE.EXE` and the
  driver DLL.
- `tests/dosbox-x/*.EXE`, `tests/dosbox-x/vbe_smoke.o`: generated test
  binaries.
- `tests/dosbox-x/capture/`: raw capture frames, ignored by the bundle's own
  `.gitignore`. The selected frames already in `evidence/` are staged.

Not part of v23 and not staged: the `guest-disposable` Windows for
Workgroups 3.11 tree in `S3Win16Recovery-v22`, and all third-party DOSBox-X
and MCP binaries in the task folders.

## Status

Evidence files are historical emulator and host-only records from the
original executor. Nothing in this snapshot was rebuilt or rerun during
recovery. Windows GDI acceptance is not claimed.
