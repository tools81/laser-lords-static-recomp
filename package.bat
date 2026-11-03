@echo off
rem Build the release packages (portable zip and installer) into dist\.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\package.ps1" %*
