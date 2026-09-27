# Lua 5.4.9 (vendored)

Upstream: https://www.lua.org/ftp/lua-5.4.9.tar.gz (released 2026-08-10),
SHA-256 `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`
(checked against lua.org's published sum). MIT license: `LICENSE`, copied
from the notice at the end of `lua.h`.

Files are unmodified copies of `src/`, **minus** what MegaMod's host
scripting must never have (docs/SCRIPTING.md): `liolib.c` (io), `loslib.c`
(os), `loadlib.c` (package/require, native modules), `ldblib.c` (debug),
`lcorolib.c` (coroutines, not needed in v1), `linit.c` (we open libraries
ourselves), and the `lua`/`luac` programs. Built as the static library
`hta_lua` (CMakeLists.txt), the same sources on desktop and Android.
