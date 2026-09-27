add_executable(apiioconfigdomain_test WIN32
    apiioconfigdomain_test.cpp apiioconfigdomain_test.h
)
target_include_directories(apiioconfigdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../../engine/test/iopluginstub
    ../../src
)

target_link_libraries(apiioconfigdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)

# Every plugin-dependent case loads engine/test/iopluginstub's DLL at run
# time (see ApiIoConfigDomain_Test::loadStubPlugin()), and reaches its test
# hooks (m_canConfigure, m_rescanCalled, emitValueChanged) through its header
# exactly like engine/test/inputpatch does.
add_dependencies(apiioconfigdomain_test iopluginstub)
