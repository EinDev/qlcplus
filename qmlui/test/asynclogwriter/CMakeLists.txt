add_executable(asynclogwriter_test WIN32
    asynclogwriter_test.cpp asynclogwriter_test.h
    ../../asynclogwriter.cpp ../../asynclogwriter.h
)

target_include_directories(asynclogwriter_test PRIVATE
    ../..
)

target_link_libraries(asynclogwriter_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
)
