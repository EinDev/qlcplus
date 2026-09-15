add_executable(showgroupmove_test WIN32
    showgroupmove_test.cpp showgroupmove_test.h
    ../../showmovehelper.cpp ../../showmovehelper.h
)

target_include_directories(showgroupmove_test PRIVATE
    ../..
)

target_link_libraries(showgroupmove_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
)
