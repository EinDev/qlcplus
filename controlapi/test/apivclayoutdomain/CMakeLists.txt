add_executable(apivclayoutdomain_test WIN32
    apivclayoutdomain_test.cpp apivclayoutdomain_test.h
    ../apivcdomain/fakevchost.cpp ../apivcdomain/fakevchost.h
)
target_include_directories(apivclayoutdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
    ../apivcdomain
)

target_link_libraries(apivclayoutdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
