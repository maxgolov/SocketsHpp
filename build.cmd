@echo off
REM Redirect to the proper PowerShell build script
powershell.exe -ExecutionPolicy Bypass -File "%~dp0scripts\Build-Windows.ps1" %*
