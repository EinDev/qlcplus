add_executable(fixtureutils_test WIN32
    fixtureutils_test.cpp fixtureutils_test.h
    ../../fixtureutils.cpp ../../fixtureutils.h
)

target_include_directories(fixtureutils_test PRIVATE
    ../..
    ../../../engine/src
)

target_link_libraries(fixtureutils_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusengine
)
