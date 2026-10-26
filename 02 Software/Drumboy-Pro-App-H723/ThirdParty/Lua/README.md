# Lua 5.4.7 (vendored)

Source: https://www.lua.org/ftp/lua-5.4.7.tar.gz — MIT license, Copyright © 1994–2024 Lua.org, PUC-Rio (full notice at the end of `src/lua.h`).

Used by the plugin host (`Core/Plugin/LuaPlugin.cpp`) to run `Plugins/*.lua` scripts from the SD card.

**Included:** the core and the `base`, `string`, `table`, `math`, `coroutine` and `utf8` libraries.
**Left out:** `lua.c`, `luac.c`, `linit.c` and the `io`, `os`, `package` and `debug` libraries (no OS, and scripts are sandboxed).

**Local changes:**
- `src/luaconf.h`: `LUA_32BITS` can be set from the build (`#if !defined(LUA_32BITS)`); the firmware builds with `LUA_32BITS=1` (32-bit integers and floats) to halve memory per value.

Lives outside `Middlewares/` so STM32CubeIDE (which compiles everything there) does not build it into stock firmware; CMake adds it only when `DRUMBOY_LUA` is on.

Build flags (see `CMakeLists.txt`): `LUA_32BITS=1`, `LUAI_MAXCCALLS=64` (bounds C-stack depth on the 16 KB main stack), compiled with `-O2` and without `-ffast-math`.
