add_executable(capabilitycounteraccumulator_test WIN32
    capabilitycounteraccumulator_test.cpp capabilitycounteraccumulator_test.h
)

target_include_directories(capabilitycounteraccumulator_test PRIVATE
    ../..
)

target_link_libraries(capabilitycounteraccumulator_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
)
