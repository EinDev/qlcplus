add_executable(apifixturesdomain_test WIN32
    apifixturesdomain_test.cpp apifixturesdomain_test.h
)
target_include_directories(apifixturesdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
)

target_link_libraries(apifixturesdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
