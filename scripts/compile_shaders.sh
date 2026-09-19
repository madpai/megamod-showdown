#!/bin/sh
# Regenerate shaders/*.inl from GLSL. Run from the repo root.
set -e
GLSLC=${GLSLC:-$(command -v glslc || echo /usr/bin/glslc)}
cd "$(dirname "$0")/.."
"$GLSLC" -mfmt=c -fshader-stage=vert shaders/mesh.vert -o shaders/mesh_vert.inl
"$GLSLC" -mfmt=c -fshader-stage=frag shaders/mesh.frag -o shaders/mesh_frag.inl
"$GLSLC" -mfmt=c -fshader-stage=vert shaders/hud.vert -o shaders/hud_vert.inl
"$GLSLC" -mfmt=c -fshader-stage=frag shaders/hud.frag -o shaders/hud_frag.inl
echo "wrote shaders/mesh_*.inl shaders/hud_*.inl"
