add_executable(rgbaudio_test WIN32
    rgbaudio_test.cpp rgbaudio_test.h
)
target_include_directories(rgbaudio_test PRIVATE
    ../../../plugins/interfaces
    ../../audio/src
    ../../src
)

target_link_libraries(rgbaudio_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusengine
)
