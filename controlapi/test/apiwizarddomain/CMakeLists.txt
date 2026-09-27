add_executable(apiwizarddomain_test WIN32
    apiwizarddomain_test.cpp apiwizarddomain_test.h
)
target_include_directories(apiwizarddomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
)

target_link_libraries(apiwizarddomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)
