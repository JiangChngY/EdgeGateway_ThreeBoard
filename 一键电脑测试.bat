@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if %errorlevel%==0 (
    py -3 tests\run_all.py
) else (
    python tests\run_all.py
)
set TEST_RESULT=%errorlevel%
echo.
if %TEST_RESULT%==0 (
    echo All PC-side tests passed.
) else (
    echo Tests failed. Please check the output above.
)
pause
endlocal & exit /b %TEST_RESULT%
