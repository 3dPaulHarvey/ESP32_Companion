@echo off
setlocal
cd /d "%~dp0"
if exist "C:\Program Files\Python313\pythonw.exe" (
    start "" "C:\Program Files\Python313\pythonw.exe" "%~dp0tools\robot_question_test.py"
    exit /b 0
)
echo The configured Python installation is missing. Ask Codex to repair this launcher.
pause
