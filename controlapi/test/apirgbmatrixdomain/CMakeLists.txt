add_executable(apirgbmatrixdomain_test WIN32
    apirgbmatrixdomain_test.cpp apirgbmatrixdomain_test.h
)
target_include_directories(apirgbmatrixdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
)

# The RGB scripts are loaded straight from the source tree so the binary can
# be run from any working directory (engine/test's CWD-relative
# INTERNAL_SCRIPTDIR only resolves under ctest's WORKING_DIRECTORY).
target_compile_definitions(apirgbmatrixdomain_test PRIVATE
    RGBSCRIPTS_DIR="${CMAKE_SOURCE_DIR}/resources/rgbscripts"
)

target_link_libraries(apirgbmatrixdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
