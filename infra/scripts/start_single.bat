@echo off
setlocal

:: 获取脚本所在目录
set SCRIPT_DIR=%~dp0
:: 去掉最后的反斜杠
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

echo Starting TinyIM Single-Node Environment...

:: 复制单节点环境配置
copy /Y "%SCRIPT_DIR%\..\..\configs\config_single.json" "%SCRIPT_DIR%\..\..\configs\config.json"

:: 切换到 docker-compose.yml 所在目录
cd /d "%SCRIPT_DIR%\..\compose"

:: 启动 docker-compose，强制构建
docker-compose -f docker-compose-single.yml up -d --build

echo TinyIM Single-Node services started.

