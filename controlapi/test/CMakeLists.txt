project(test)

add_subdirectory(apienvelope)
add_subdirectory(apiiodomain)
add_subdirectory(apicoredomain)
add_subdirectory(apifunctionsdomain)
add_subdirectory(apipalettedomain)
add_subdirectory(apifixturesdomain)
add_subdirectory(apifixturegroupdomain)
add_subdirectory(apivcdomain)
add_subdirectory(apirgbmatrixdomain)
add_subdirectory(apiefxcollectiondomain)
add_subdirectory(apimediadomain)
add_subdirectory(apivclayoutdomain)
add_subdirectory(apishowdomain)
add_subdirectory(apimonitordomain)
add_subdirectory(apifixtureremapdomain)
add_subdirectory(apifixturedefsdomain)
add_subdirectory(apiioconfigdomain)
add_subdirectory(apivcinputdomain)
add_subdirectory(apivclivedomain)
add_subdirectory(apifixturechannelsdomain)
add_subdirectory(webserver)

# Registered with CTest the same way engine/test/CMakeLists.txt does (see
# that file for why enable_testing() itself only needs to be called once,
# at the repo root).
set(QLCPLUS_CONTROLAPI_TESTS
    apienvelope_test
    apiiodomain_test
    apicoredomain_test
    apifunctionsdomain_test
    apipalettedomain_test
    apifixturesdomain_test
    apifixturegroupdomain_test
    apivcdomain_test
    apirgbmatrixdomain_test
    apiefxcollectiondomain_test
    apimediadomain_test
    apivclayoutdomain_test
    apishowdomain_test
    apimonitordomain_test
    apifixtureremapdomain_test
    apifixturedefsdomain_test
    apiioconfigdomain_test
    apivcinputdomain_test
    apivclivedomain_test
    apifixturechannelsdomain_test
    webserver_test
)
foreach(_qlcplus_test_target ${QLCPLUS_CONTROLAPI_TESTS})
    add_test(NAME ${_qlcplus_test_target}
             COMMAND $<TARGET_FILE:${_qlcplus_test_target}>
             WORKING_DIRECTORY $<TARGET_FILE_DIR:${_qlcplus_test_target}>)
endforeach()

add_custom_target(controlapi_tests DEPENDS ${QLCPLUS_CONTROLAPI_TESTS})
