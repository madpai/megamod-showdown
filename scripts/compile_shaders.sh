#!/bin/sh
# Regenerate shaders/*.inl from GLSL. Run from the repo root.
set -e
GLSLC=${GLSLC:-$(command -v glslc || echo /usr/bin/glslc)}
cd "$(dirname "$0")/.."
"$GLSLC" -mfmt=c -fshader-stage=vert shaders/mesh.vert -o shaders/mesh_vert.inl
"$GLSLC" -mfmt=c -fshader-stage=frag shaders/mesh.frag -o shaders/mesh_frag.inl
echo "wrote shaders/mesh_vert.inl shaders/mesh_frag.inl"
