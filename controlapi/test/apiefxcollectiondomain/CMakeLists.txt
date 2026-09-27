add_executable(apiefxcollectiondomain_test WIN32
    apiefxcollectiondomain_test.cpp apiefxcollectiondomain_test.h
)
target_include_directories(apiefxcollectiondomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
)

target_link_libraries(apiefxcollectiondomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
