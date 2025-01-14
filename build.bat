@echo off

REM Compile the compiler
set SOURCE_FILES=compiler/src/onyx.c compiler/src/astnodes.c compiler/src/builtins.c compiler/src/checker.c compiler/src/clone.c compiler/src/doc.c compiler/src/entities.c compiler/src/errors.c compiler/src/lex.c compiler/src/parser.c compiler/src/symres.c compiler/src/types.c compiler/src/utils.c compiler/src/wasm_emit.c compiler/src/wasm_runtime.c

if "%1" == "1" (
    set FLAGS=/Od /MTd /Z7
) else (
    set FLAGS=/O2 /MT /Z7
)

for /f "delims=" %%i in ('cd') do set PWD=%%i

set LINK_OPTIONS="%PWD%\shared\lib\windows_x86_64\lib\wasmer.lib" ws2_32.lib Advapi32.lib userenv.lib bcrypt.lib kernel32.lib /NODEFAULTLIB:msvcrt.lib /NODEFAULTLIB:libcmtd.lib /NODEFAULTLIB:msvcrtd.lib
set FLAGS=%FLAGS% "/I%PWD%\shared\include" /DENABLE_RUN_WITH_WASMER=1

rc.exe misc/icon_resource.rc
cl.exe %FLAGS% /Icompiler/include /std:c17 /TC %SOURCE_FILES% /link /IGNORE:4217 %LINK_OPTIONS% /DEBUG /OUT:onyx.exe /incremental:no /opt:ref /subsystem:console misc\icon_resource.res

del *.pdb > NUL 2> NUL
del *.ilk > NUL 2> NUL
del *.obj > NUL 2> NUL
del misc\icon_resource.res

REM Compile the onyx-run tool
set SOURCE_FILES=compiler/src/onyxrun.c compiler/src/wasm_runtime.c

rc.exe misc/icon_resource.rc
cl.exe %FLAGS% /Icompiler/include /std:c17 /TC %SOURCE_FILES% /link /IGNORE:4217 %LINK_OPTIONS% /DEBUG /OUT:onyx-run.exe /incremental:no /opt:ref /subsystem:console misc\icon_resource.res

del *.pdb > NUL 2> NUL
del *.ilk > NUL 2> NUL
del *.obj > NUL 2> NUL
del misc\icon_resource.res

cl /MT /std:c17 /TC /I compiler/include /I shared/include /D_USRDLL /D_WINDLL runtime\onyx_runtime.c /link /DLL ws2_32.lib /OUT:onyx_runtime.dll

del onyx_runtime.obj
del onyx_runtime.lib
del onyx_runtime.exp