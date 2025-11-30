@echo off
setlocal

set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

echo Stopping TinyIM containers...

cd /d "%SCRIPT_DIR%\..\compose"

:: Stop HA environment
echo Checking HA environment...
docker-compose -f docker-compose-ha.yml down 2>nul
if %ERRORLEVEL%==0 echo HA environment stopped.

:: Stop Single-Node environment
echo Checking Single-Node environment...
docker-compose -f docker-compose-single.yml down 2>nul
if %ERRORLEVEL%==0 echo Single-Node environment stopped.

:: Cleanup
docker-compose down 2>nul

echo.
echo All TinyIM services stopped.
echo.
pause
