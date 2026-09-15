add_executable(spoutframegate_test WIN32
    spoutframegate_test.cpp spoutframegate_test.h
    ../../spoutframegate.cpp ../../spoutframegate.h
)

target_include_directories(spoutframegate_test PRIVATE
    ../..
)

target_link_libraries(spoutframegate_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
)
