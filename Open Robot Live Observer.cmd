@echo off
cd /d "%~dp0"
python tools\robot_live_observer.py --seconds 180
pause
