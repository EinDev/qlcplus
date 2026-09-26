add_executable(avolitesd4parser_test WIN32
    avolitesd4parser_test.cpp avolitesd4parser_test.h
)
target_include_directories(avolitesd4parser_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)

target_link_libraries(avolitesd4parser_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusengine
)
