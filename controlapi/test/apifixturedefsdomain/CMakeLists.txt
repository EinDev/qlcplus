add_executable(apifixturedefsdomain_test WIN32
    apifixturedefsdomain_test.cpp apifixturedefsdomain_test.h
)
target_include_directories(apifixturedefsdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
)

target_link_libraries(apifixturedefsdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
