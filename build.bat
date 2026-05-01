@echo off
REM === 构建脚本 - CoroutineServer + TestClient ===
REM 用法: build.bat [Debug|Release]
REM 需要: VS2022 + vcpkg (或手动配置的依赖路径)
setlocal enabledelayedexpansion

set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Debug

echo ============================================
echo   Building CoroutineServer (%BUILD_TYPE%, x64)
echo ============================================

REM 检测 MSBuild
where msbuild >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] MSBuild 未找到, 请在 Developer Command Prompt 中运行
    exit /b 1
)

echo.
echo [1/2] 编译服务器 CoroutineServer...
msbuild CoroutineServer\CoroutineServer.vcxproj /p:Configuration=%BUILD_TYPE% /p:Platform=x64 /m /nologo /v:minimal
if %errorlevel% neq 0 (
    echo [ERROR] 服务器编译失败!
    exit /b 1
)

echo.
echo [2/2] 编译客户端 TestClient...
msbuild TestClient\TestClient.vcxproj /p:Configuration=%BUILD_TYPE% /p:Platform=x64 /m /nologo /v:minimal
if %errorlevel% neq 0 (
    echo [ERROR] 客户端编译失败!
    exit /b 1
)

echo.
echo ============================================
echo   Build SUCCESS!
echo   Server: CoroutineServer\x64\%BUILD_TYPE%\CoroutineServer.exe
echo   Client: TestClient\x64\%BUILD_TYPE%\TestClient.exe
echo ============================================

REM 复制可执行文件到当前目录方便使用
copy /Y "CoroutineServer\x64\%BUILD_TYPE%\CoroutineServer.exe" . >nul
copy /Y "TestClient\x64\%BUILD_TYPE%\TestClient.exe" . >nul
echo.
echo 可执行文件已复制到项目根目录: CoroutineServer.exe, TestClient.exe
endlocal
