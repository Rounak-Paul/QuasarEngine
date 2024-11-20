@echo off
setlocal enabledelayedexpansion

echo "Compiling shaders..."

set vulkanSDKPath=%1

if "%vulkanSDKPath%"=="" (
    echo VULKAN_SDK path not provided. Please provide it as an argument.
    exit /b 1
)

REM Directory paths
set shaderDir=Assets/Shaders
set outputDir=bin/Shaders
set glslcPath=%vulkanSDKPath%\bin\glslc.exe

REM Check if output directory exists, if not, create it
if not exist "%outputDir%" (
    echo Output directory does not exist. Creating "%outputDir%".
    mkdir "%outputDir%"
)

REM Loop over all .glsl files in shaderDir
for %%f in (%shaderDir%\*.glsl) do (
    REM Get the full file name and extension
    set "filepath=%%f"
    set "filename=%%~nf"
    set "extension=%%~xf"

    REM Determine shader stage based on file extension suffix
    if "!filename:~-4!" == "vert" (
        set "stage=vert"
    ) else if "!filename:~-4!" == "frag" (
        set "stage=frag"
    ) else if "!filename:~-4!" == "comp" (
        set "stage=comp"
    ) else (
        echo Skipping unknown shader type: !filepath!
        continue
    )

    REM Output file path
    set "outputfile=%outputDir%\!filename!.spv"
    echo "!filepath! -> !outputfile!"

    REM Compile shader
    "%glslcPath%" -fshader-stage=!stage! !filepath! -o !outputfile!
    if %ERRORLEVEL% NEQ 0 (
        echo Error: %ERRORLEVEL%
        exit /b %ERRORLEVEL%
    )
)

echo "Done."
