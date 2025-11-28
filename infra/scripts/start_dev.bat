@echo off
setlocal

:: 获取脚本所在目录
set SCRIPT_DIR=%~dp0
:: 去掉最后的反斜杠
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

echo Starting TinyIM development environment...

:: 切换到 docker-compose.yml 所在目录
cd /d "%SCRIPT_DIR%\..\compose"

:: 启动 docker-compose，强制构建，确保 Dockerfile 变动生效
docker-compose up -d --build

echo TinyIM services started.
pause
