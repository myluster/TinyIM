@echo off
setlocal

:: Get script directory
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

echo Resetting TinyIM HA Database...
cd /d "%SCRIPT_DIR%\..\compose"

:: Stop and remove containers and volumes
docker-compose -f docker-compose-ha.yml down -v

echo Database reset complete. Please run start_ha.bat to restart.

