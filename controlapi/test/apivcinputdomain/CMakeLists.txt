add_executable(apivcinputdomain_test WIN32
    apivcinputdomain_test.cpp apivcinputdomain_test.h
    ../apivcdomain/fakevchost.cpp ../apivcdomain/fakevchost.h
)
target_include_directories(apivcinputdomain_test PRIVATE
    ../../../plugins/interfaces
    ../../../engine/src
    ../../../engine/test/iopluginstub
    ../../src
    ../apivcdomain
)

target_link_libraries(apivcinputdomain_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusapiserver
    qlcplusengine
)

# The auto-detect cases load engine/test/iopluginstub's DLL at run time and
# make it emit an input signal (same setup as controlapi/test/apiioconfigdomain).
add_dependencies(apivcinputdomain_test iopluginstub)
