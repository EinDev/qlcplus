# Vendored Spout2 SDK subset (SpoutDX)

This directory contains an unmodified-except-where-noted subset of the
[Spout2](https://github.com/leadedge/Spout2) SDK, used by QLC+ 5's Spout
video output on Windows (`qmlui/spoutsender.{h,cpp}`).

- Upstream repository: https://github.com/leadedge/Spout2
- Upstream commit: `c2bcc12147711d12ace7d5f08e869d774d840f8a` (2026-07-19)
- Vendored on: 2026-09-15
- License: BSD-2-Clause (see `LICENSE` in this directory, copied verbatim
  from the upstream repository root). Compatible with QLC+'s Apache-2.0.

## Files taken

Only the DirectX 11 path (`SpoutDX`) and the common classes it depends on.
No OpenGL (`SpoutGL.cpp`, `Spout.cpp`, `SpoutSender.cpp`, `SpoutReceiver.cpp`,
`SpoutGLextensions.*`) and none of the DX9/DX12 variants. Note that the
SDK's own `SpoutSender.h` (not vendored) declares a class also named
`SpoutSender`; QLC+'s wrapper class of the same name in `qmlui/spoutsender.h`
is unrelated, so do not add that upstream file here.

From `SPOUTSDK/SpoutDirectX/SpoutDX/`:

- `SpoutDX.cpp`
- `SpoutDX.h`

From `SPOUTSDK/SpoutGL/`:

- `SpoutCommon.h`
- `SpoutCopy.cpp`, `SpoutCopy.h`
- `SpoutDirectX.cpp`, `SpoutDirectX.h`
- `SpoutFrameCount.cpp`, `SpoutFrameCount.h`
- `SpoutSenderNames.cpp`, `SpoutSenderNames.h`
- `SpoutSharedMemory.cpp`, `SpoutSharedMemory.h`
- `SpoutUtils.cpp`, `SpoutUtils.h`

From the repository root:

- `LICENSE`

All files are placed flat in this directory. `SpoutDX.h` uses
`__has_include("SpoutCommon.h")` to pick the same-folder include layout, so
this needs no path patching.

## Build integration

`CMakeLists.txt` here (ours, not upstream's) builds the subset as a static
library `spoutdx`, `if(WIN32)` only, with `SPOUT_BUILD_STATIC`, `-msse4` on
MinGW (SpoutCopy uses SSE intrinsics) and the explicit list of Windows
system libraries the SDK otherwise pulls in via MSVC-only
`#pragma comment(lib, ...)`. Warnings are suppressed for this target only
and the include directory is exported as `SYSTEM`, so the project's
`-Werror` applies neither to the vendored sources nor to warnings that
originate inside these headers when a project source includes them.

`qmlui/CMakeLists.txt` adds this directory unconditionally (it contributes
nothing outside WIN32) and, on WIN32 only, links `spoutdx` into `qlcplus5`
and defines `QLC_SPOUT`. Everything Spout-related in QLC+'s own sources is
guarded by `#if defined(Q_OS_WIN) && defined(QLC_SPOUT)`.

## Local patches

Every local modification to the vendored sources is marked in place with a
comment starting with `// QLC+ MinGW patch:`. Grep for that string to find
them all. Current list:

- None. The upstream commit above already carries the MinGW fixes from
  upstream PRs #93, #114 and #122, and the subset compiled unmodified with
  MSYS2 MinGW-w64 GCC 16.2 (`-msse4`, warnings suppressed for this target
  only). If a future upstream bump needs a patch, add it here and mark it
  in the source.

## Updating

1. `git clone --depth 1 https://github.com/leadedge/Spout2` somewhere
   outside the repository, note the commit hash and date.
2. Copy the files listed above over the ones here (flat layout), plus
   `LICENSE`.
3. Re-apply any patches listed above and update this file (hash, date,
   patch list).
4. Rebuild the `spoutdx` target first (`cmake --build build --target spoutdx`),
   then `qlcplus5`, and run the `QLCPLUS_DEBUG_SPOUT=1` smoke hook in
   `qmlui/main.cpp`.
