# DOSBox-X guest session status

Read-only check: 2026-10-09

- PID 5504 is still running. `Get-Process` reports image path `C:\DOSBox-X\dosbox-x.exe`, start time `2026-10-09 16:55:39`, SessionId 1, a nonzero MainWindowHandle (`0xA059E`), and title `CODEX Win3.11 stock VGA baseline (disposable copy) - DOSBox-X 2026.08.31: KRNL386 - 100%`.
- The caller's PowerShell and Explorer are also in SessionId 1; `quser` reports user `codex` on active console session 1. This shows a window/process in the active console session, but does not establish that it is visible or foreground on Pavel's current desktop. The native UI bridge is unavailable, so no visibility or foreground check was performed.
- Pavel's Task Manager extraction names PID 5504 as `dosbox.x86.exe`; this executor reports the same PID as `C:\DOSBox-X\dosbox-x.exe`. The PID/start time tie the local process to the DOSBox-X instance started by this task, but the displayed image-name difference is not independently reconciled.
- WMI's `CommandLine` field was blank. This task launched it with `-conf C:\CODEX\2026-10-09\task-2\S3Win16Recovery-v22\dosbox-windows-stock-baseline.conf` (the exact invocation used by this task).
- Config C: mount resolves to `C:\CODEX\2026-10-09\task-2\S3Win16Recovery-v22\guest-disposable\DOS`. Staging is `C:\CODEX\2026-10-09\task-2\S3Win16Recovery-v22\guest-disposable\DOS\S3TEST`.
- Staged `S3PROBE.EXE` and `S3WIDE.DRV` hashes still match the verified v23 inputs. Original `C:\CODEX\DOS\WINDOWS\SYSTEM.INI` SHA-256 equals the disposable copy's SHA-256 (`bf0dd1eb2128313284dbbe719aebfff724dd8674245844eff86a37f5baa04cae`); the original `C:\CODEX\DOS\S3TEST` does not exist.

Do not start another DOSBox-X instance with this config while PID 5504 is running; it would mount the same disposable guest tree. After that instance exits, the recorded launch command is:

```text
"C:\DOSBox-X\dosbox-x.exe" -conf "C:\CODEX\2026-10-09\task-2\S3Win16Recovery-v22\dosbox-windows-stock-baseline.conf"
```

## Graceful close and safe relaunch

- A single `CloseMainWindow()` request was sent after rechecking PID 5504's executable path, start time, session, and title. It returned `True`, but the process did not exit within 10 seconds; its title changed to `Quit DOSBox-X warning`. The confirmation prompt was left untouched. No force termination or UI interaction was performed. PID 5504 therefore remains a blocker to launching another instance against this guest tree.
- Added `C:\CODEX\2026-10-09\task-2\Launch-Win311-Disposable.ps1`. It checks the executable/config/guest paths, refuses when any `dosbox-x` process exists, acquires an exclusive CreateNew lock file to prevent simultaneous launcher invocations, rechecks DOSBox-X after taking the lock, and starts the configured instance in a normal visible window while waiting for its exit. It removes its lock on normal completion. PowerShell parser validation passed; it was not run because PID 5504 is still active.
- After PID 5504 and any other DOSBox-X process have exited, launch with `powershell.exe -NoProfile -File "C:\CODEX\2026-10-09\task-2\Launch-Win311-Disposable.ps1"`. Do not use an execution-policy bypass. If a stale lock remains after an abnormal launcher termination, first confirm no DOSBox-X process is running, then remove `C:\CODEX\2026-10-09\task-2\.win311-disposable-launch.lock` before retrying.
