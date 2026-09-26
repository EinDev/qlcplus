add_executable(qlcinputsource_test WIN32
    qlcinputsource_test.cpp qlcinputsource_test.h
)
target_include_directories(qlcinputsource_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)

target_link_libraries(qlcinputsource_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusengine
)
