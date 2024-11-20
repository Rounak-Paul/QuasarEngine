#!/bin/bash

echo "Compiling shaders..."

vulkanSDKPath=$1

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
    filename=$(basename -- "$filepath")
    filename_no_ext="${filename%.*}"

    # Determine shader stage based on file extension suffix
    if [[ "$filename_no_ext" == *"vert" ]]; then
        stage="vert"
    elif [[ "$filename_no_ext" == *"frag" ]]; then
        stage="frag"
    elif [[ "$filename_no_ext" == *"comp" ]]; then
        stage="comp"
    else
        echo "Skipping unknown shader type: $filepath"
        continue
    fi

    # Output file path
    outputfile="$outputDir/$filename_no_ext.spv"
    echo "$filepath -> $outputfile"

    # Compile shader
    "$glslcPath" -fshader-stage=$stage "$filepath" -o "$outputfile"
    if [ $? -ne 0 ]; then
        echo "Error: $?"
        exit $?
    fi
done

echo "Done."