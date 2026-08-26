add_executable(apienvelope_test WIN32
    apienvelope_test.cpp apienvelope_test.h
    ../../src/apienvelope.cpp ../../src/apienvelope.h
)
target_include_directories(apienvelope_test PRIVATE
    ../../src
)

target_link_libraries(apienvelope_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
)
