@echo off
setlocal
set ERR=0

echo S3Wide environment check
echo ========================

if "%WIN16DDK%"=="" (
  echo [MISSING] WIN16DDK environment variable
  set ERR=1
) else (
  if exist "%WIN16DDK%\286\TOOLS" (
    echo [OK] Win16 DDK tools: %WIN16DDK%\286\TOOLS
  ) else (
    echo [MISSING] %WIN16DDK%\286\TOOLS
    set ERR=1
  )
  if exist "%WIN16DDK%\286\INC" (
    echo [OK] Win16 DDK includes: %WIN16DDK%\286\INC
  ) else (
    echo [MISSING] %WIN16DDK%\286\INC
    set ERR=1
  )
)

where exe2bin.exe >nul 2>nul
if errorlevel 1 (
  echo [MISSING] EXE2BIN.EXE is not in PATH
  set ERR=1
) else (
  echo [OK] EXE2BIN.EXE found
)

if exist "external\s3-original" (
  echo [OK] external\s3-original exists
) else (
  echo [INFO] external\s3-original not present
)

if exist "external\dosbox-x" (
  echo [OK] external\dosbox-x exists
) else (
  echo [INFO] external\dosbox-x not present; source may be cloned separately
)

if exist "external\vbesvga" (
  echo [OK] external\vbesvga exists
) else (
  echo [INFO] external\vbesvga not present; source may be cloned separately
)

echo.
if "%ERR%"=="0" (
  echo Environment baseline looks usable.
  exit /b 0
) else (
  echo Environment is incomplete. See external\README.md.
  exit /b 1
)
