# Header-only helper (virtualconsole/vcselectionprune.h): no qmlui sources, no engine.
add_executable(vcselectionprune_test WIN32
    vcselectionprune_test.cpp vcselectionprune_test.h
    ../../virtualconsole/vcselectionprune.h
)

target_include_directories(vcselectionprune_test PRIVATE
    ../../virtualconsole
)

target_link_libraries(vcselectionprune_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
)
