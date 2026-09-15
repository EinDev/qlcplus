add_executable(functionpathutils_test WIN32
    functionpathutils_test.cpp functionpathutils_test.h
    ../../functionpathutils.cpp ../../functionpathutils.h
)

target_include_directories(functionpathutils_test PRIVATE
    ../..
)

target_link_libraries(functionpathutils_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
)
