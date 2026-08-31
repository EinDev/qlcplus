add_executable(apifunctionsdomain_test WIN32
    apifunctionsdomain_test.cpp apifunctionsdomain_test.h
)
target_include_directories(apifunctionsdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
)

target_link_libraries(apifunctionsdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
