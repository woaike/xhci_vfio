@echo off
REM upload_to_target.bat - Windows版本
REM 需要设置环境变量: JUMP_PASS 和 TARGET_PASS

if "%JUMP_PASS%"=="" (
    echo Error: JUMP_PASS environment variable not set
    exit /b 1
)
if "%TARGET_PASS%"=="" (
    echo Error: TARGET_PASS environment variable not set
    exit /b 1
)

set JUMP_HOST=10.65.10.1
set JUMP_USER=jinzixiang
set TARGET_HOST=10.65.46.174
set TARGET_USER=root
set REMOTE_DIR=/home/hygon/jzx/xhci-test

echo === Uploading files to %TARGET_HOST% ===

REM Create remote directories
sshpass -p "%JUMP_PASS%" ssh -o StrictHostKeyChecking=no ^
    -o "ProxyCommand=sshpass -p ^"%JUMP_PASS%^" ssh -o StrictHostKeyChecking=no -W %%h:%%p %JUMP_USER%@%JUMP_HOST%" ^
    %TARGET_USER%@%TARGET_HOST% "mkdir -p %REMOTE_DIR%/src %REMOTE_DIR%/include %REMOTE_DIR%/build"

REM Upload source files
for %%f in (src\xhci_enum.c src\xhci_ops.c src\xhci_state.c src\vfio.c src\xhci_bot.c src\main.c src\test_xhci.c src\test_example.c src\test_bot.c include\xhci.h include\xhci_internal.h include\xhci_regs.h include\xhci_state.h include\xhci_bot.h Makefile) do (
    if exist "%%f" (
        echo Uploading %%f...
        sshpass -p "%JUMP_PASS%" scp -o StrictHostKeyChecking=no ^
            -o "ProxyCommand=sshpass -p ^"%JUMP_PASS%^" ssh -o StrictHostKeyChecking=no -W %%h:%%p %JUMP_USER%@%JUMP_HOST%" ^
            "%%f" "%TARGET_USER%@%TARGET_HOST%:%REMOTE_DIR%/%%f"
    ) else (
        echo Warning: %%f not found, skipping
    )
)

echo.
echo === Building on target ===
sshpass -p "%JUMP_PASS%" ssh -o StrictHostKeyChecking=no ^
    -o "ProxyCommand=sshpass -p ^"%JUMP_PASS%^" ssh -o StrictHostKeyChecking=no -W %%h:%%p %JUMP_USER%@%JUMP_HOST%" ^
    %TARGET_USER%@%TARGET_HOST% "cd %REMOTE_DIR% && rm -rf build && make"

echo.
echo === Done! ===
echo To run tests:
echo   sshpass -p %%JUMP_PASS%% ssh -o StrictHostKeyChecking=no -o "ProxyCommand=sshpass -p ^"%%JUMP_PASS%%^" ssh -o StrictHostKeyChecking=no -W %%h:%%p %JUMP_USER%@%JUMP_HOST%" root@%TARGET_HOST% "cd %REMOTE_DIR% && ./build/main --enum"
