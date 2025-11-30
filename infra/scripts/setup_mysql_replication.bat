@echo off
setlocal

echo Setting up MySQL Replication...

:: Wait for MySQL Master to be ready
echo Waiting for MySQL Master to be fully ready...
:wait_master
set /a RETRIES=0
:check_master
docker exec tinyim_mysql_master mysqladmin ping -h localhost -uroot -proot_password >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo MySQL Master is ready!
    goto master_ready
)
set /a RETRIES+=1
if %RETRIES% LSS 30 (
    echo Waiting for MySQL Master... (%RETRIES%/30)
    timeout /t 2 /nobreak >nul
    goto check_master
)
echo ERROR: MySQL Master failed to start
exit /b 1

:master_ready
:: Wait for Slaves to be ready
echo Waiting for MySQL Slaves to be ready...
timeout /t 5 /nobreak >nul

:: 1. Create Replication User on Master
echo Creating replication user on Master...
docker exec tinyim_mysql_master mysql -uroot -proot_password -e "DROP USER IF EXISTS 'repl'@'%%'; CREATE USER 'repl'@'%%' IDENTIFIED BY '123456'; GRANT REPLICATION SLAVE ON *.* TO 'repl'@'%%'; FLUSH PRIVILEGES;"
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to create replication user
    exit /b 1
)

:: 2. Get Master Status
echo Getting Master Status...
for /f "tokens=1,2" %%A in ('docker exec tinyim_mysql_master mysql -uroot -proot_password -e "SHOW MASTER STATUS\G" ^| findstr "File Position"') do (
    if "%%A"=="File:" set MASTER_LOG_FILE=%%B
    if "%%A"=="Position:" set MASTER_LOG_POS=%%B
)

echo Master Log File: %MASTER_LOG_FILE%
echo Master Log Pos: %MASTER_LOG_POS%

if "%MASTER_LOG_FILE%"=="" (
    echo Failed to get Master Status
    exit /b 1
)

:: 2.5 Dump Master Data and Import to Slaves
echo Dumping Master data...
docker exec tinyim_mysql_master mysqldump -uroot -proot_password --all-databases --master-data > "%TEMP%\master_dump.sql"

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to dump Master data
    exit /b 1
)

echo Importing data to Slave 1...
type "%TEMP%\master_dump.sql" | docker exec -i tinyim_mysql_slave_1 mysql -uroot -proot_password
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to import data to Slave 1
    exit /b 1
)

echo Importing data to Slave 2...
type "%TEMP%\master_dump.sql" | docker exec -i tinyim_mysql_slave_2 mysql -uroot -proot_password
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to import data to Slave 2
    exit /b 1
)

:: Clean up dump file
del "%TEMP%\master_dump.sql"

:: 3. Configure Slave 1
echo Configuring Slave 1...
docker exec tinyim_mysql_slave_1 mysql -uroot -proot_password -e "STOP SLAVE; RESET SLAVE ALL; CHANGE MASTER TO MASTER_HOST='tinyim_mysql_master', MASTER_USER='repl', MASTER_PASSWORD='123456', MASTER_LOG_FILE='%MASTER_LOG_FILE%', MASTER_LOG_POS=%MASTER_LOG_POS%; START SLAVE;"

:: 4. Configure Slave 2
echo Configuring Slave 2...
docker exec tinyim_mysql_slave_2 mysql -uroot -proot_password -e "STOP SLAVE; RESET SLAVE ALL; CHANGE MASTER TO MASTER_HOST='tinyim_mysql_master', MASTER_USER='repl', MASTER_PASSWORD='123456', MASTER_LOG_FILE='%MASTER_LOG_FILE%', MASTER_LOG_POS=%MASTER_LOG_POS%; START SLAVE;"

echo Replication setup complete.
echo.
echo Checking Slave 1 status:
docker exec tinyim_mysql_slave_1 mysql -uroot -proot_password -e "SHOW SLAVE STATUS\G" | findstr "Slave_IO_Running Slave_SQL_Running"
echo.
echo Checking Slave 2 status:
docker exec tinyim_mysql_slave_2 mysql -uroot -proot_password -e "SHOW SLAVE STATUS\G" | findstr "Slave_IO_Running Slave_SQL_Running"

