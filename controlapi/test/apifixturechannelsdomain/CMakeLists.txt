add_executable(apifixturechannelsdomain_test WIN32
    apifixturechannelsdomain_test.cpp apifixturechannelsdomain_test.h
)
target_include_directories(apifixturechannelsdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
)

target_link_libraries(apifixturechannelsdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
