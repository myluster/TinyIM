@echo off
setlocal

:: Get script directory
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

echo Running TinyIM Tests...
cd /d "%SCRIPT_DIR%\..\compose"

echo.
echo === Entering Development Container ===
echo.

:: Run functional tests
echo [1/4] Running Functional Tests...
docker-compose -f docker-compose-single.yml exec -T tinyim-dev bash -c "cd /app/build/tests && ./functional_tests ../../configs/config.json"
set FUNCTIONAL_RESULT=%ERRORLEVEL%

echo [2/4] Running Status Tests...
docker-compose -f docker-compose-single.yml exec -T tinyim-dev bash -c "cd /app/build/tests && ./status_tests ../../configs/config.json"
set STATUS_RESULT=%ERRORLEVEL%

echo [3/4] Running Offline Msgs Tests...
docker-compose -f docker-compose-single.yml exec -T tinyim-dev bash -c "cd /app/build/tests && ./offline_msgs_tests ../../configs/config.json"
set OFFLINE_RESULT=%ERRORLEVEL%

echo.
echo ========================================
echo.

:: Optionally run stress tests
if "%1"=="--stress" (
    echo [2/2] Running Stress Tests...
    set THREADS=5
    set ITERATIONS=50
    
    if not "%2"=="" set THREADS=%2
    if not "%3"=="" set ITERATIONS=%3
    
    echo Running with %THREADS% threads, %ITERATIONS% iterations each...
    docker-compose -f docker-compose-single.yml exec -T tinyim-dev bash -c "cd /app/build/tests && ./stress_tests %THREADS% %ITERATIONS%"
    set STRESS_RESULT=%ERRORLEVEL%
) else (
    echo [2/2] Skipping Stress Tests (use --stress flag to run)
    set STRESS_RESULT=0
)

echo.
echo ========================================
echo === Test Results Summary ===
echo ========================================

if %FUNCTIONAL_RESULT%==0 (
    echo [PASS] Functional Tests
) else (
    echo [FAIL] Functional Tests
)

if %STATUS_RESULT%==0 (
    echo [PASS] Status Tests
) else (
    echo [FAIL] Status Tests
)

if %OFFLINE_RESULT%==0 (
    echo [PASS] Offline Msgs Tests
) else (
    echo [FAIL] Offline Msgs Tests
)

if "%1"=="--stress" (
    if %STRESS_RESULT%==0 (
        echo [PASS] Stress Tests
    ) else (
        echo [FAIL] Stress Tests
    )
)

echo ========================================
echo.

if %FUNCTIONAL_RESULT% NEQ 0 (
    echo Functional tests failed.
    exit /b 1
)
if %STATUS_RESULT% NEQ 0 (
    echo Status tests failed.
    exit /b 1
)
if %OFFLINE_RESULT% NEQ 0 (
    echo Offline Msgs tests failed.
    exit /b 1
)

echo All tests passed!
exit /b 0
