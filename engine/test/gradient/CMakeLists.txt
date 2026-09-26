add_executable(gradient_test WIN32
    gradient_test.cpp gradient_test.h
)
target_include_directories(gradient_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)

target_link_libraries(gradient_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusengine
)
