add_executable(apicoredomain_test WIN32
    apicoredomain_test.cpp apicoredomain_test.h
)
target_include_directories(apicoredomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../../qmlui
    ../../src
)

target_link_libraries(apicoredomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Quick
    qlcplusapiserver
    qlcplusengine
)
