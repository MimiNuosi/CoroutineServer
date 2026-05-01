@echo off
REM === 启动服务器 ===
REM 运行前确保已 delete im.db 以清空数据库

if not exist CoroutineServer.exe (
    echo [ERROR] 请先运行 build.bat 编译项目
    pause
    exit /b 1
)

echo ============================================
echo   CoroutineServer 启动
echo   监听端口: 8080
echo   数据库: im.db
echo   按 Ctrl+C 停止服务器
echo ============================================
echo.

CoroutineServer.exe
pause
