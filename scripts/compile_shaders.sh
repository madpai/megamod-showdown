#!/bin/sh
# Regenerate shaders/*.inl from GLSL. Run from the repo root.
# Uses glslc when present, else glslangValidator (Ubuntu's glslang-tools);
# both produce the brace-wrapped word list gfx_vulkan.c #includes.
set -e
cd "$(dirname "$0")/.."
GLSLC=${GLSLC:-$(command -v glslc || true)}
GLSLANG=$(command -v glslangValidator || true)

build() { # stage src out
    if [ -n "$GLSLC" ]; then
        "$GLSLC" -mfmt=c -fshader-stage="$1" "$2" -o "$3"
    elif [ -n "$GLSLANG" ]; then
        tmp="$3.tmp"
        "$GLSLANG" -V -S "$1" -x -o "$tmp" "$2" >/dev/null
        { echo "{"; grep -v '^\s*//' "$tmp"; echo "}"; } > "$3"
        rm -f "$tmp"
    else
        echo "need glslc or glslangValidator" >&2; exit 1
    fi
}

build vert shaders/mesh.vert shaders/mesh_vert.inl
build frag shaders/mesh.frag shaders/mesh_frag.inl
build vert shaders/hud.vert  shaders/hud_vert.inl
build frag shaders/hud.frag  shaders/hud_frag.inl
build vert shaders/post.vert shaders/post_vert.inl
build frag shaders/post.frag shaders/post_frag.inl
echo "wrote shaders/{mesh,hud,post}_*.inl"
