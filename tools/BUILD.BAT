@echo off
setlocal
call "%~dp0CHECKENV.BAT"
if errorlevel 1 exit /b 1

if not exist "src" (
  echo [BLOCKED] src directory/source baseline has not been established yet.
  echo Complete M1 research before wiring the actual driver build.
  exit /b 2
)

echo [TODO] Build command will be pinned after the buildable S3 source baseline is selected.
exit /b 2
