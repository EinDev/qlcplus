project(test)

add_subdirectory(treeflatmodel)
add_subdirectory(treemodel)
add_subdirectory(functionpathutils)
add_subdirectory(fixtureutils)
add_subdirectory(arrangegeometry)
add_subdirectory(asynclogwriter)
add_subdirectory(capabilitycounteraccumulator)
add_subdirectory(showgroupmove)
add_subdirectory(spoutframegate)
add_subdirectory(shortcutmanager)

# Convenience aggregate target, mirroring engine/test's `engine_tests`:
# `cmake --build build --target qmlui_tests` builds every suite above without
# a full project build. They stay out of CTest on purpose (see CLAUDE.md,
# "Unit testing (qmlui/test)"); dev-test-coverage.ps1 and CI run the binaries
# directly.
add_custom_target(qmlui_tests DEPENDS
    arrangegeometry_test
    asynclogwriter_test
    capabilitycounteraccumulator_test
    fixtureutils_test
    functionpathutils_test
    shortcutmanager_test
    showgroupmove_test
    spoutframegate_test
    treeflatmodel_test
    treemodel_test
)
