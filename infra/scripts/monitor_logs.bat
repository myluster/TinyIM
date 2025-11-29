@echo off
setlocal

:: Get script directory
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

echo Monitoring TinyIM Service Logs...
cd /d "%SCRIPT_DIR%\..\compose"

:: Tail logs for auth, chat, and gateway
docker-compose -f docker-compose.yml exec tinyim-dev tail -f /app/auth.log /app/chat.log /app/gateway.log
