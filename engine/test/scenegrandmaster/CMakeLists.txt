add_executable(scenegrandmaster_test WIN32
    ../common/resource_paths.h
    scenegrandmaster_test.cpp scenegrandmaster_test.h
)
target_include_directories(scenegrandmaster_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)

target_link_libraries(scenegrandmaster_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusengine
)
