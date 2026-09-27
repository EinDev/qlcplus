add_executable(apivclivedomain_test WIN32
    apivclivedomain_test.cpp apivclivedomain_test.h
    ../apivcdomain/fakevchost.cpp ../apivcdomain/fakevchost.h
)
target_include_directories(apivclivedomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
    ../apivcdomain
)

target_link_libraries(apivclivedomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
