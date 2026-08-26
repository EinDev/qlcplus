project(test)

add_subdirectory(apienvelope)
add_subdirectory(apiiodomain)

# Registered with CTest the same way engine/test/CMakeLists.txt does (see
# that file for why enable_testing() itself only needs to be called once,
# at the repo root).
set(QLCPLUS_CONTROLAPI_TESTS
    apienvelope_test
    apiiodomain_test
)
foreach(_qlcplus_test_target ${QLCPLUS_CONTROLAPI_TESTS})
    add_test(NAME ${_qlcplus_test_target}
             COMMAND $<TARGET_FILE:${_qlcplus_test_target}>
             WORKING_DIRECTORY $<TARGET_FILE_DIR:${_qlcplus_test_target}>)
endforeach()

add_custom_target(controlapi_tests DEPENDS ${QLCPLUS_CONTROLAPI_TESTS})
