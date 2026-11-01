# S3 Windows 3.11 recovery handoff

**Status date:** 2026-11-01  
**Purpose:** transfer current repository and recoverable project state without
claiming a working Windows driver or recovery of inaccessible local files.

## Current repository state verified on 2026-11-01

The authenticated GitHub connection resolves the private repository
`TheKernelDoc/S3` (repository ID `1392059144`) and reports push/admin access for
the TheKernelDoc account. The ordinary unauthenticated Git HTTPS `ls-remote`
attempt returned `Repository not found`; the repository was then verified
through the authorized GitHub connection.

- `main` points to `d77dfee19aa9cbcd48d3d479a0b652f178121712`.
- `main` is the original project scaffold. Its tree has `AGENTS.md`, README,
  docs, external-dependency notes, DOSBox-X config, and placeholder build/env
  scripts. There is no `src/` directory or driver source on `main`.
- Existing branch `transfer/s3-wfw311-loader-canary-20261012` points to
  `94b2f4b439f22f7dc4d4cfe3ad5b88b4b89eb280`; it adds a loader-canary ZIP and
  `.sha256` file. The committed checksum text is
  `5de4966f6c02cc5d6ef9a4f0f676599cd507274d9b49b91533878f5fa3bec7e1`.
  This branch is pre-existing and is not being rewritten.
- Neither branch contains the full S3 driver source from the historical
  `/workspace/s3wide-recovery` checkout.

This handoff is being published as
`docs/RECOVERY_HANDOFF_20261101.md` on the additive branch
`recovery/20261101-project-handoff`, based on the verified transfer-branch
commit above. The recovery branch inherits the existing canary transfer and
adds the handoff only. It does not alter `main` or the existing transfer
branch. The branch head SHA is recorded in the publication report accompanying
this file.

This execution environment has no `/workspace/s3wide-recovery` checkout.
`/workspace/Atalanta` was left untouched. The prior source tree and its local
uncommitted modifications are not present here.

The Library search found the latest main recovery package metadata:

- Name: `S3Win16Recovery-20261012-font-layout-review.zip`
- Library item: `libfile_e4ac2e60b6fc8191b66dc1ee6e43f8f4`
- Version: 70; reported size 1,272,751 bytes; modified 2026-10-12
- The approved Library materialization helper returned HTTP 401. Retrieval was
  stopped. This environment did not read the archive bytes, inspect its member
  list, or calculate its checksum.

Other relevant retained Library items from conversation history/search:

- `S3Win16StandardModePilot-20261009-v1.zip`, item
  `libfile_dd15832a5e5881918b8a1e3ec634fad7`, version 3, 96,253 bytes;
  last recorded archive SHA-256
  `16593f948dfacf1024f03f1b45196ddcdd9118d7b825ce2fe9c586049c6f9007`.
- `S3-WfW311-Loader-Canary-20261011.zip`, item
  `libfile_222029ed14c48191abb3f19106a55a74`, version 1, 55,289 bytes;
  last recorded archive SHA-256
  `5de4966f6c02cc5d6ef9a4f0f676599cd507274d9b49b91533878f5fa3bec7e1`.
- Those hashes/versions are historical records, not rechecked in the current
  environment. No item was downloaded or changed here.

Further historical artifact leads, not present in the current workspace:

| Artifact | Recorded identity / limitation |
|---|---|
| Main recovery archive v22 | `S3Win16Recovery-20261009-v22.zip`; source, built driver and tests were reported; destination was requested as `C:\CODEX`; exact digest not recovered here. Its Library item is the v70 recovery file above. |
| Post-restart harness evidence | Library `libfile_36feed92612c8191838a1ebb9bb59feb`; contents not read here. |
| Public-GDI diagnostic | `S3DBBTR-diagnostic-20261012.zip`, Library `libfile_d9198c942e448191ab749893ad85f859` v2; recorded ZIP SHA-256 `98a4f20681deac464a9bc56068e1f6c9f8b53b9d34a56ba682690318e75cb453`; CRC and 11 manifest entries were reported passing. Diagnostic executable SHA-256 `0d82af3d4cf28658332e20fff0db56ee8bd52030a7056a2afae62c576b0ed5bf`, reportedly unchanged between package versions. No guest run. |
| Narrow source/header/test bundle | Library `libfile_356f074478508191a7c57978d4549010`, 52,743 bytes, recorded SHA-256 `bd5a99c406fa72c34820df824908fde48c68b29d2e82994707ae6d7ae88bd31b`. |
| Host alias candidate bundle | Library `libfile_b7b67bec64a081919118b919732a3bd3` v0, 25,095 bytes, seven files, recorded SHA-256 `89d216ab4b482967ef4aecc8c55f6eeecc834f385298a3ebec84d00814d91518`. |
| Font-layout reproduction | Historical commit `2db39c9c8905ebefdfb60348753562f3724882fb`; its source tree was not in this environment. |

Keep these references as recovery leads only. Rebind every recovered file to
its exact bytes, version and manifest before treating any historical hash or
test report as current evidence.

The Windows CODEX paths below are user-supplied historical records and were not
accessed or verified in this environment. The machine is reported online but
not authorized for this task, so this is not a fresh inventory:

- QA task `01a122ed-7b20-77c4-aee8-94846e3b6033`, historical folder
  `C:\CODEX\2026-10-09\task-3\dosbox-harness-retry`
- Preparation task `01a12cb3-827f-75e7-a357-97ca810c4023`, historical folder
  `C:\CODEX\2026-10-11\task-2\s3-vxcanary-prep`

Additional historical paths, also unverified here, were `C:\CODEX\GIT\S3`
and read-only external checkout `C:\CODEX\GIT\S3\external\vbesvga` at
`622f38512555a42ed324af1aa62adfefbccd8f2a`. Reuse rights for VBESVGA were not
established. The local repository was historically managed read-only; do not
bypass that restriction.

Historical records say the October 9 post-recycle checks preserved the local
harness/source/result hashes, confirmed the original guest was unchanged, and
found no stale PID 11440. October 11 reported 277 matching files between the
disposable guest and rollback baseline, original untouched. These details
remain unverified in this environment.

On October 12 at 02:56, two loader-canary ZIP copies were reported checksum
verified locally, superseding an earlier missing-ZIP blocker. At 03:08, the
claim of a usable visible emulator window was retracted: a Win32 visible flag
and handle did not mean the user could see or control it. No loader-canary
guest execution was established by that correction.

## Last known project state from prior work reports

These are handoff facts from prior task messages, not fresh checks of the
current filesystem. Preserve their scope and qualification when reusing them.

### Target and guest

- Target: DOSBox-X S3 1280x720 with a usable 8-bit Windows GDI driver; ultimate
  target includes 16M-color Windows GDI. DOS bitmap tests are a separate proof.
- Historical accepted score: 30/100.
- The 1280x720x8 first acceptance target is a genuine guest resolution in
  Windows 3.1/3.11 Enhanced Mode under DOSBox-X `svga_s3trio64`; host-side
  scaling does not count. Preserve the existing S3 modes and S3 acceleration
  where possible; VESA is diagnostic/reference only, not a silent replacement.
- DOS work separately targeted hardware-style 4:3-to-16:9 bitmap text and
  graphics without TrueType or stretching workarounds, with fixed-mode
  fallback. DOS multi-monitor was only a feasibility discussion.
- Reported Windows 3.11 baseline: Program Manager in Enhanced Mode, `VGA.DRV`
  plus `*VDDVGA`, 640x480. `vmemsize=-1` was configured; resolved VRAM capacity
  was not logged.
- Reported custom-driver startup stalled in USER after `DisplayResources`
  and before fonts. The stock VGA/S3 control booted. This is not acceptance of
  the custom driver.
- A reported Enhanced Mode probe returned `GetWinFlags=0x0C29` (Enhanced set,
  Standard clear). `WIN.COM /S /B` returned to DOS without a boot-log change;
  that probe was not an Enhanced-mode solution.
- No verified end-to-end Windows GDI 1280x720 acceptance, 8-bit or 16M-color,
  is available in the present environment.

The repository's `AGENTS.md` requires a disposable guest, known-good VGA/stock
S3 recovery path, logs or durable research notes for material experiments, and
boot plus visible guest rendering before calling a mode accepted. It also says
not to commit proprietary Microsoft/S3 binaries or documentation without
redistribution permission.

### Implementation and test work historically reported

- The source work included the Win16 DDA driver, 8-bit GDI paths, selector and
  framebuffer bridge, access guards, host-only packed-24 surface routines,
  color objects, text/font investigations, DIB pathways, and DOSBox-X S3 mode
  probes. Host models do not establish Windows GDI behavior or physical S3
  behavior.
- A 16-bit arithmetic defect in the frame-size constant was corrected to
  calculate 921,600 bytes, with bank 14 as the final 64-KiB window. Host tests
  exercised invalid ranges, selector limit, bank bound, and callback failures.
- Ownership-revocation and cross-device raster adversarial cases led to
  fail-closed surface/cursor changes and host regressions. A cached-bank probe
  showed that the earlier path could reuse an aperture after a fake VM-owner
  change; this was explicitly not evidence of a native VDD failure.
- A follow-up static fix rejected cross-device raster identities before
  writes and repaired safe cleanup after denied invisible-cursor Show. Prior
  reports said strict C89 host and ASan/UBSan suites passed; no guest/runtime
  test followed from those results.
- A separate packed-24 host surface was added. Its DIB BGR byte order and
  DWORD-aligned DIB stride were sourced, but S3-specific 1280x720 packed-24
  scanout order/pitch/alignment remained unverified. No 24-bpp hardware claim.
- The standard-mode pilot was a separate, deliberately fail-closed DLL. It
  checked a mode gate and had host/build/NE checks. No mode programming, bank
  path, active DDA Enable acceptance, or Enhanced Mode success was established.
- A standalone Win16 loader canary package was prepared. Its proposed guest
  route was not a Windows driver acceptance test. No VDD or Windows setup
  change was authorized or reported as made.
- Source audits found public VBESVGA/VDD and DOSBox-X S3 reference material,
  but rights and custom-driver compatibility were unestablished. No source
  code was copied from the Microsoft-copyrighted reference tree and no
  candidate VDD/grabber was installed. VDD plus enhanced-grabber compatibility
  remains a required pair-level gate.

### Historical milestone and test ledger

**Reported as demonstrated, with limits:**

- October 8: clean WfW desktop and exit; stock S3 640x480x32 desktop baseline;
  stock 1024x768x32 and 1280x1024x8 verified. Stock 1280x1024x32 was black /
  inconclusive. An S3 register experiment reached 1280x720x8 scanout, but
  Windows GDI still reported 1280x1024. A later cold 1280x720 capture was
  reviewed; neither is a custom widescreen GDI desktop.
- DOS 160x45 bitmap text probe passed in DOSBox with a screenshot and without
  TrueType; broader DOS application compatibility was not verified.
- October 9: isolated Win16 execution and an S3 loader harness reportedly
  passed on CODEX. A manual probe loaded exports, reported GDIINFO 1280x720x8,
  preserved the resource ID, and returned device Enable 0. Driver hash
  `3902af42d12e36713914081e9f0ef193c86b1df8768e847930f662bf150c49fe`
  identifies that historical candidate. It does not establish a working GDI
  desktop.
- Host-only raster work reportedly included bounded PBITMAP snapshots,
  short-palette rejection, pixel-format/24-bpp DIB/ROP checks, cached-bank
  ownership denial, cursor restore/End retry, StrBlt End-to-Disable failures,
  row locators and guarded DIB4 integration. Individual strict C89 and
  sanitizer runs are not evidence that every revision passes one unified suite.
- A 30-byte host-only wire-frame decoder passed boundary tests. The 64-KiB and
  1-MiB boundary cases do not establish a native provider contract.
- A stock VGA ordinal-19 lookup returned `020F:0D1E` without invocation.
  Later natural stock ordinal-19 breakpoint observations and public GDI
  Set/Get tests were reported, but they do not establish the custom driver.
- October 12 Run5 captured 32-byte PBITMAP metadata: 7x4, widthBytes 2, four
  planes by one bit, `bmWidthPlanes=8`; digest
  `E9DB8644CD234C498180347EBEBE078E48105878CA1BFECE08192C5D1B94B41C`. No
  embedded-pointer reads were made. A normal log/finalizer was observed;
  emulator exit code was not recorded; companion exit 0.
- October 12 font-layout reproduction found vertical 8-pixel strips where a
  row-major layout had been assumed. A synthetic 12x14 glyph expected 6
  pixels, with 2 correct and 3 extra before correction; extent was correct.
  The approved fix reportedly passed host regressions and sanitizers, but real
  guest font payload was absent.
- Numeric selector-span host tests covered inclusive zero limit, no
  `limit+1` overflow and nonzero offsets. Selector limits prove addressability
  only, not allocation ownership or permission.

**Failed, incomplete, or unrun:**

- The custom display path failed before device Enable / at resource or CR13
  readback stages; production Enable remains fail-closed.
- Run4 was incomplete with a forced stop. Do not transfer Run5 success to it.
- Run6 did launch on October 11 at 09:53:06.301779Z for about 28.8 seconds.
  Only seed SetDIBits 0/5 stage was reached (return 5, guards 1); abort host
  check failed with no predicate logged. No partial Set/Get 2/2 proof.
- The VxD canary had static 500-byte LE/DDB validation and no hardware API;
  no `Device_Init` or guest execution was demonstrated in the final record.
- A diagnostic v2 contained a public-GDI one-hot fixture and breakpoint/
  capture instructions, but no guest run was reported.
- No real-font allocation/ownership, matching native provider, usable custom
  GDI, 16M-color Windows application, or full DOS compatibility was accepted.

### Import and link identity gate

- Target raw normal-driver import fixups: KERNEL ordinals
  `{3,4,175,176,177,186,187,188,189}`, USER ordinal `{1}`. The name-to-ordinal
  mapping remains unresolved. The Standard Mode pilot additionally has KERNEL
  ordinal `132`, but the mapping to `GetWinFlags` is not authoritative.
- `StrBlt` is the driver's own DDA export ordinal 11, not a Windows import.
- An earlier ordinal-permutation test reportedly kept the structural inspector
  output unchanged after swapping fixups; this demonstrated that the old
  inspector did not prove import identity.
- In the prior workspace, `src/driver_smoke.lnk` selected Open Watcom's
  `lib286/win/windows.lib`; the library was absent at its recorded
  `/tmp/ow-linux-x64/lib286/win/windows.lib` path. The current environment
  cannot inspect this claim.
- Exact input needed: legitimate, exact-build target WfW 3.11 KERNEL and USER
  NE images (Enhanced Mode may use `KRNL386.EXE`) or authoritative matching
  import data with provenance and rights basis. For actual link proof, also
  preserve the exact Open Watcom `windows.lib` or its verifiable import records
  and compare them with target exports.
- Source-level API prototypes were documented in the prior workspace using a
  public copy of the Windows 3.1 SDK `WINDOWS.H`; that copy is not an
  authenticated target release export table.

## Previously reported build and verification commands

### Current repository scaffold

The current `main` checkout's `tools/BUILD.BAT` calls `CHECKENV.BAT`, then
reports `[BLOCKED] src directory/source baseline has not been established yet`
or `[TODO]` and exits 2. `tools/CHECKENV.BAT` expects `WIN16DDK` to identify a
tree containing `286\TOOLS` and `286\INC`, plus `EXE2BIN.EXE` in PATH. The
repository's `external/README.md` asks for a legally obtained Win16 DDK,
contemporary Visual C++, EXE2BIN, and local-only S3/reference materials. These
are scaffold checks, not commands that build the recovered historical driver.
`test/dosbox-x.conf` configures `machine=svga_s3trio64` and `vmemsize=4`; config
values alone are not scanout or Windows acceptance.

### Historical source workspace commands

The following commands were reported as passing in the prior executor; none
could be rerun here because the checkout/toolchain is absent. Do not present
them as current-environment runs.

```sh
WATCOM=/tmp/ow-linux-x64 OWBIN=binl64 ./build.sh
WATCOM=/tmp/ow-linux-x64 OWBIN=binl64 ./build-standard-mode-pilot.sh
WATCOM=/tmp/ow-linux-x64 OWBIN=binl64 ./tests/win16/build-standard-pilot-probe.sh
```

The source workspace also had host C89 suites run through `build.sh`, including
host and provider tests, surface24, truecolor object, DIB conversion, text,
cursor, and geometry suites. A specific historical report recorded strict
C89 and ASan/UBSan host suites passing after access-guard fixes. These are
historical command reports only: no corresponding logs or binaries were
present to independently inspect here.

The historical setup separately used a licensed Open Watcom tree at
`/tmp/ow-linux-x64` with `OWBIN=binl64`. Its October 10 archive was reported as
129,081,007 bytes with SHA-256
`f047f68afabd89b6f6f44a8cd91b9c6c5c8c39b7133425a11fc662f692b0325b`; license
acceptance and the pin update were reported approved October 11. That tree is
not available here. A stale pin
`cd922f6200a20505cdda44dc0155471ae3d3f53e33a354097f3d89de74dc49c6` must not be
silently reused.

The historical DDK/VC path was `c/TOOLKIT/WIN31/DDK` with bundled VC 1.52C and
FreeDOS EXE2BIN under `c/FDTOOLS`. A separate unchanged proprietary Video7 DDK
sample reportedly built with MASM 5.10A, LINK 5.20.034, RC 3.11, MAPSYM and
EXE2BIN 1.5a. That is toolchain proof only, not S3 driver acceptance or source
redistribution permission. These files are not in the GitHub tree and are not
available in this environment.

Historical worker paths also included `/workspace/shared/s3wide-project/Native/Win16Diagnostics`,
`/workspace/shared/s3wide-tools/root` (Debian DOSBox-X 2025.02.01+dfsg-3 SDL2),
and `/workspace/shared/s3wide-guest`; they were not present here.

Historical DOSBox-X debugger work had been blocked by official Debian
Autoconf/Automake/m4/Libtool/ncurses development prerequisites. A later note
described a possible debugger route, but its executable build options and
guest acceptance remained unverified. Do not claim the debugger route ran.

The October 19 import-preflight addition was separately reported to pass
`PYTHONDONTWRITEBYTECODE=1 python3 -m unittest -v
tests/test_win16_import_preflight.py` (seven synthetic-fixture tests), with
the no-input CLI returning `IMPORT_IDENTITY_UNRESOLVED` and exit code 2. Those
tests validate parsing/fail-closed behavior only; they do not establish any
Windows ordinal mapping. They were not rerun in this environment.

## Known blockers and remaining work

1. **Source recovery:** the active workspace has no S3 checkout. The current
   Library archive metadata is known, but approved materialization returned
   HTTP 401; the Windows CODEX machine is reported unauthorized. Provide an
   authorized source archive/workspace and obtain CODEX authorization before
   inventorying local files.
2. **Selective publication:** inventory recovered manifests, source, tests,
   docs, build outputs and provenance before staging. Include project-owned
   source, tests, scripts,
   documentation, and a detailed evidence handoff. Exclude credentials,
   proprietary DDK/Windows binaries, third-party S3 driver/VDD/grabber binaries,
   unrelated files, and generated packages that bundle restricted material.
   Preserve original evidence and clearly label emulator/host-only data.
3. **Import mapping:** resolve each name/module/ordinal against legitimate
   target-version artifacts and the exact selected Open Watcom import library;
   add a fixture-backed validator that rejects the previous ordinal mutant.
4. **Enhanced Mode:** establish rights-cleared, version-matched S3 DDA/VDD/
   enhanced-grabber ABI and VM ownership/lifecycle behavior before enabling
   hardware access. Native Enable remains fail-closed until this is proven.
5. **Actual GDI acceptance:** after gates above, test Windows 3.11 boot, 8-bit
   desktop, drawing/text/DIB paths at 1280x720, and separately validate the
   16M-color path. Keep DOS bitmap proof separate. Preserve exact guest image,
   config, logs, screenshots, hashes, and failure states. Do not fabricate
   acceptance evidence.
6. **Further publication:** the recovery handoff branch preserves the prior
   `main` and transfer branch. Publish any newly recovered code only after
   inspection and selective staging; do not force-push or replace remote
   history. Record branch and commit SHAs from a fresh remote read.

## Acceptance scorecard carry-forward

Historical overall score remains **30/100, provisional**; this handoff awards
no points. Per-category earned points and all category acceptance records were
not recovered. The following weights sum to 100:

| Gate | Weight | Evidence still needed |
|---|---:|---|
| Reproducible build setup | 8 | Bind source commit, licensed toolchain, exact commands, outputs and hashes. |
| Mode/banked scanout proof | 6 | Retain emulator experiment evidence and distinguish it from driver integration. |
| Raster/cursor primitives | 6 | Bind host tests to exact source; add native validation. |
| Win16 loader/ABI | 5 | Loader is partial; import/provider identity unresolved. |
| Usable 8-bit GDI | 20 | Real custom desktop, drawing, ownership, transitions and recovery. |
| 16M-color GDI | 30 | Actual custom true-color GDI and application proof. |
| Windows apps/recovery | 15 | Representative apps, mode changes and rollback. |
| DOS bitmap scanout/compatibility | 10 | Broader app compatibility beyond the 160x45 probe. |

## Unpushed/inaccessible material

- The previous checkout `/workspace/s3wide-recovery` and all local source,
  tests, generated outputs, uncommitted modifications, and October 19 import
  preflight files are absent from this environment. No exact exhaustive file
  list can be asserted. Prior task summaries identified these October 19 files
  in that workspace:
  `evidence/win16-import-contract-20261019.json`,
  `evidence/win16-import-preflight-20261019.md`,
  `tools/win16_import_preflight.py`, and
  `tests/test_win16_import_preflight.py`.
- Prior recorded SHA-256 values for those four files (not reverified now):
  `7c86b95dfeb790a2a83bf3e75b28e58a88c74c5d13d5c980ef27bedf73e2fc7d`,
  `e2281ccd34bb6a0ce0ea1899ed99acd08471db8d272042644173642f3a8ac9cf`,
  `f8a3b8e31a80702c723077d55fc4bdf913d555db667cc0e1965d9ba33a043ac9`, and
  `5329e22fd202a49d58f7582c77d3afabc2324728f69a11550cedc3f3edcfe055`.
- This recovery publication contains this handoff document only. It does not
  claim the unpushed source tree, October 19 files, CODEX work, or Library v70
  archive have been recovered. The branch/base/commit status is recorded in the
  publication summary accompanying this document.
