add_executable(apifunctionsmiscdomain_test WIN32
    apifunctionsmiscdomain_test.cpp apifunctionsmiscdomain_test.h
    ../apivcdomain/fakevchost.cpp ../apivcdomain/fakevchost.h
)
target_include_directories(apifunctionsmiscdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../../engine/audio/src
    ../../src
    ../apivcdomain
)

# RGB scripts from the source tree (see apirgbmatrixdomain's CMakeLists.txt)
target_compile_definitions(apifunctionsmiscdomain_test PRIVATE
    RGBSCRIPTS_DIR="${CMAKE_SOURCE_DIR}/resources/rgbscripts"
)

# functions.audio.detectBpm needs a real decoder: load the freshly built
# sndfile plugin when this build has one (the test skips the BPM half otherwise)
if(TARGET sndfileplugin)
    add_dependencies(apifunctionsmiscdomain_test sndfileplugin)
    target_compile_definitions(apifunctionsmiscdomain_test PRIVATE
        SNDFILE_PLUGIN_DIR="$<TARGET_FILE_DIR:sndfileplugin>"
    )
endif()

target_link_libraries(apifunctionsmiscdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
