# Compiles webserver.cpp directly (like apienvelope_test) instead of linking
# qlcplusapiserver: WebServer has no engine/Doc dependency and this keeps it
# that way - if this target ever needs qlcplusengine, something coupled
# WebServer to the engine that shouldn't have.
add_executable(webserver_test WIN32
    webserver_test.cpp webserver_test.h
    ../../src/webserver.cpp ../../src/webserver.h
)
target_include_directories(webserver_test PRIVATE
    ../../src
)

target_link_libraries(webserver_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Network
    Qt${QT_MAJOR_VERSION}::Test
)
