@echo off
setlocal

:: 获取脚本所在目录
set SCRIPT_DIR=%~dp0
:: 去掉最后的反斜杠
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

echo Starting TinyIM HA Environment...

:: 复制高可用环境配置
copy /Y "%SCRIPT_DIR%\..\..\configs\config_ha.json" "%SCRIPT_DIR%\..\..\configs\config.json"

:: 切换到 docker-compose.yml 所在目录
cd /d "%SCRIPT_DIR%\..\compose"

:: 启动 docker-compose，强制构建
docker-compose -f docker-compose-ha.yml up -d --build

echo TinyIM HA services started.

:: Setup MySQL Replication (script will wait for MySQL to be ready)
echo Setting up MySQL Replication...
call "%SCRIPT_DIR%\setup_mysql_replication.bat"

echo HA environment startup complete.



