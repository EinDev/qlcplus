add_executable(apifixtureremapdomain_test WIN32
    apifixtureremapdomain_test.cpp apifixtureremapdomain_test.h
)
target_include_directories(apifixtureremapdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
)

target_link_libraries(apifixtureremapdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
