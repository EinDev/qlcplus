# shortcutmanager.cpp needs QLCFile (engine) for the default overrides path
# and the generated engine/src/qlcconfig.h - same engine link as
# fixtureutils_test. Qt::Gui is for QKeyEvent/QKeySequence.
add_executable(shortcutmanager_test WIN32
    shortcutmanager_test.cpp shortcutmanager_test.h
    ../../shortcutmanager.cpp ../../shortcutmanager.h
)

target_include_directories(shortcutmanager_test PRIVATE
    ../..
    ../../../engine/src
)

target_link_libraries(shortcutmanager_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusengine
)
