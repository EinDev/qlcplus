add_executable(apitoolsdomain_test WIN32
    apitoolsdomain_test.cpp apitoolsdomain_test.h
)
target_include_directories(apitoolsdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
)

target_link_libraries(apitoolsdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)

