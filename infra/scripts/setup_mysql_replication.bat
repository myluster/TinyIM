@echo off
setlocal

echo Setting up MySQL Replication...

:: 1. Create Replication User on Master
echo Creating replication user on Master...
docker exec tinyim_mysql_master mysql -uroot -proot_password -e "DROP USER IF EXISTS 'repl'@'%'; CREATE USER 'repl'@'%' IDENTIFIED BY '123456'; GRANT REPLICATION SLAVE ON *.* TO 'repl'@'%'; FLUSH PRIVILEGES;"

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

:: 3. Configure Slave 1
echo Configuring Slave 1...
docker exec tinyim_mysql_slave_1 mysql -uroot -proot_password -e "STOP SLAVE; CHANGE MASTER TO MASTER_HOST='tinyim_mysql_master', MASTER_USER='repl', MASTER_PASSWORD='123456', MASTER_LOG_FILE='%MASTER_LOG_FILE%', MASTER_LOG_POS=%MASTER_LOG_POS%; START SLAVE;"

:: 4. Configure Slave 2
echo Configuring Slave 2...
docker exec tinyim_mysql_slave_2 mysql -uroot -proot_password -e "STOP SLAVE; CHANGE MASTER TO MASTER_HOST='tinyim_mysql_master', MASTER_USER='repl', MASTER_PASSWORD='123456', MASTER_LOG_FILE='%MASTER_LOG_FILE%', MASTER_LOG_POS=%MASTER_LOG_POS%; START SLAVE;"

echo Replication setup complete.
docker exec tinyim_mysql_slave_1 mysql -uroot -proot_password -e "SHOW SLAVE STATUS\G" | findstr "Slave_IO_Running Slave_SQL_Running"
