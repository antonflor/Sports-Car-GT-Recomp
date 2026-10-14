@echo off
rem Installs Sports Car GT: 2026 Edition (asks for administrator rights).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
