add_executable(apiiodomain_test WIN32
    apiiodomain_test.cpp apiiodomain_test.h
)
target_include_directories(apiiodomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../src
)

target_link_libraries(apiiodomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)

# The io.plugin.list / io.patch.* cases load engine/test/iopluginstub's
# plugin DLL at runtime (see ApiIoDomain_Test::loadStubPlugin()) - make sure
# it exists whenever this test binary does.
add_dependencies(apiiodomain_test iopluginstub)
