# Vendored Spout2 SDK subset (SpoutDX - the DirectX 11 sender/receiver
# path only, no OpenGL). See README.md in this directory for provenance,
# license and the list of local patches.
#
# Only the Windows build gets this target; on every other platform this
# directory contributes nothing and qmlui compiles the Spout feature out
# (see QLC_SPOUT in ../CMakeLists.txt).

if(WIN32)
    set(SPOUTDX_SOURCES
        SpoutDX.cpp
        SpoutCopy.cpp
        SpoutDirectX.cpp
        SpoutFrameCount.cpp
        SpoutSenderNames.cpp
        SpoutSharedMemory.cpp
        SpoutUtils.cpp
    )
    set(SPOUTDX_HEADERS
        SpoutDX.h
        SpoutCommon.h
        SpoutCopy.h
        SpoutDirectX.h
        SpoutFrameCount.h
        SpoutSenderNames.h
        SpoutSharedMemory.h
        SpoutUtils.h
    )

    add_library(spoutdx STATIC ${SPOUTDX_SOURCES} ${SPOUTDX_HEADERS})

    # Same definition upstream's own SpoutDX_static target uses.
    target_compile_definitions(spoutdx PRIVATE SPOUT_BUILD_STATIC)

    # SYSTEM so that project sources including SpoutDX.h (compiled with the
    # project's -Werror -Wall -Wextra) don't fail on warnings that originate
    # inside the vendored headers (e.g. MSVC-only #pragma comment(lib, ...)).
    target_include_directories(spoutdx SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    if(MSVC)
        target_compile_options(spoutdx PRIVATE /W0)
    else()
        # Third-party code: don't apply the project's warning policy to it.
        # This is target-scoped, the project's own sources keep -Werror.
        target_compile_options(spoutdx PRIVATE -w)
    endif()

    # SpoutCopy uses SSE2/SSSE3/SSE4 intrinsics; upstream adds the same flag
    # for MinGW (SpoutGL/CMakeLists.txt, "MINGW AND NOT SPOUT_BUILD_ARM").
    if(MINGW AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|aarch64")
        target_compile_options(spoutdx PRIVATE -msse4)
    endif()

    # GCC ignores the SDK's #pragma comment(lib, ...) directives, so every
    # system library it needs has to be linked explicitly. PUBLIC so they
    # propagate to whatever links this static library.
    target_link_libraries(spoutdx PUBLIC
        d3d11
        dxgi
        d3d9
        shell32
        advapi32
        version
        comctl32
        psapi
        ole32
        user32
        gdi32
        winmm
    )
endif()
