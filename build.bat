@echo off

py -OO -m nuitka --onefile --standalone --follow-imports --include-plugin-files=scripts\config.py --include-plugin-files=scripts\encoder.py --include-plugin-files=scripts\vs_script.py --include-plugin-files=scripts\render.py main.py --output-dir="%TEMP%" -o "Smoothie.exe"
timeout 5