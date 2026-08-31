add_executable(apifixturegroupdomain_test WIN32
    apifixturegroupdomain_test.cpp apifixturegroupdomain_test.h
)
target_include_directories(apifixturegroupdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
)

target_link_libraries(apifixturegroupdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
