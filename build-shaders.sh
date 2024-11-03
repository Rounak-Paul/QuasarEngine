#!/bin/bash

echo "Compiling shaders..."

vulkanSDKPath="$1"

if [ -z "$vulkanSDKPath" ]; then
    echo "VULKAN_SDK path not provided. Please provide it as an argument."
    exit 1
fi

# Directory paths
shaderDir="Assets/Shaders"
outputDir="bin/Shaders"
glslcPath="$vulkanSDKPath/bin/glslc"

# Check if output directory exists, if not, create it
if [ ! -d "$outputDir" ]; then
    echo "Output directory does not exist. Creating \"$outputDir\"."
    mkdir -p "$outputDir"
fi

# Loop over all .glsl files in shaderDir
for filepath in "$shaderDir"/*.glsl; do
    # Check if there are any .glsl files
    if [ ! -e "$filepath" ]; then
        echo "No shader files found in $shaderDir."
        break
    fi

    # Get the filename and extension
    filename=$(basename "$filepath")
    extension="${filename##*.}"

    # Echo the name of the shader source file
    echo "Processing shader file: $filename"

    # Determine shader stage based on file extension suffix
    case "${filename: -4}" in
        .vert)
            stage="vert"
            ;;
        .frag)
            stage="frag"
            ;;
        .comp)
            stage="comp"
            ;;
        *)
            echo "Skipping unknown shader type: $filepath"
            continue
            ;;
    esac

    # Output file path
    outputfile="$outputDir/${filename%.glsl}.spv"
    echo "$filepath -> $outputfile"

    # Compile shader
    "$glslcPath" -fshader-stage="$stage" "$filepath" -o "$outputfile"
    if [ $? -ne 0 ]; then
        echo "Error: Compilation failed for $filename"
        exit 1
    fi
done

echo "Done."
