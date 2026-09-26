add_executable(rgbimage_test WIN32
    rgbimage_test.cpp rgbimage_test.h
)
target_include_directories(rgbimage_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)

target_link_libraries(rgbimage_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusengine
)
