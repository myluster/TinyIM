@echo off
setlocal

:: Get script directory
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

echo Running TinyIM Backend Services (HA)...
cd /d "%SCRIPT_DIR%\..\compose"

:: Execute run_services.sh inside the container using HA compose file with HA config
docker-compose -f docker-compose-ha.yml exec tinyim-dev bash /app/infra/scripts/run_services.sh configs/config_ha.json
