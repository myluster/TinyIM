@echo off
setlocal

:: Get script directory
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

echo Building TinyIM Backend Services...
cd /d "%SCRIPT_DIR%\..\compose"

:: Run make inside the container
:: Run cmake and make inside the container
docker-compose -f docker-compose-single.yml exec -T tinyim-dev bash -c "cd /app/build && cmake .. && make -j4"
