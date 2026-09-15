add_executable(mediaassets_test WIN32
    mediaassets_test.cpp mediaassets_test.h
)
target_include_directories(mediaassets_test PRIVATE
    ../../../plugins/interfaces
    ../../audio/src
    ../../src
)

target_link_libraries(mediaassets_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusaudio
    qlcplusengine
)
