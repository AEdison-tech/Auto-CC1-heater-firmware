@echo off
cd /d "%~dp0"

git add -A
if errorlevel 1 goto :error

set /p MSG="Commit message (Enter for 'update'): "
if "%MSG%"=="" set MSG=update

git commit -m "%MSG%"
if errorlevel 1 (
    echo Nothing to commit.
    goto :done
)

git push origin master
if errorlevel 1 goto :error

:done
echo.
echo Done.
pause
exit /b 0

:error
echo.
echo ERROR - something went wrong.
pause
exit /b 1
