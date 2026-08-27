project(scripts)

set(SCRIPT_FILES
    alternate.js
    balls.js
    blinder.js
    circles.js
    circular.js
    evenodd.js
    fill.js
    fillfromcenter.js
    fillunfill.js
    fillunfillfromcenter.js
    fillunfillsquaresfromcenter.js
    fireworks.js
    flyingobjects.js
    gradient.js
    lines.js
    marquee.js
    noise.js
    onebyone.js
    opposite.js
    plasma.js
    randomcolumn.js
    randomfillcolumn.js
    randomfillrow.js
    randomfillsingle.js
    randompixelperrow.js
    randompixelperrowmulticolor.js
    randomrow.js
    randomsingle.js
    sinewave.js
    snowbubbles.js
    squares.js
    squaresfromcenter.js
    starfield.js
    stripes.js
    stripesfromcenter.js
    strobe.js
    verticalfall.js
    waves.js
)

# engine/test/common/resource_paths.h's INTERNAL_SCRIPTDIR is a path relative
# to a test binary's own directory (see engine/test/CMakeLists.txt, which sets
# WORKING_DIRECTORY to each test's $<TARGET_FILE_DIR>), resolving to
# ${CMAKE_BINARY_DIR}/resources/rgbscripts - so rgbscript_test (and
# rgbalgorithm_test/rgbmatrix_test, which use the same macro) need a live copy
# of this whole directory there, refreshed whenever a script changes,
# independent of the Android-only asset copy below. Without this, a fresh
# build has no scripts there at all, and a build directory that acquired a
# copy some other way (e.g. manually) would never see it updated after
# editing a .js file - both failure modes have actually bitten test runs
# against this project. Copy the whole directory, not just SCRIPT_FILES:
# RGBScript_Test::scripts() specifically re-reads this directory's own
# CMakeLists.txt (as a "is this script registered" check), so a partial copy
# missing it would newly break that check (QFile::open(QIODevice::ReadWrite)
# silently creates an empty file when one isn't there, which then reads back
# as "no scripts are registered" instead of erroring loudly).
set(SCRIPT_FILES_ABS)
foreach(SCRIPT_FILE IN LISTS SCRIPT_FILES)
    list(APPEND SCRIPT_FILES_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT_FILE}")
endforeach()

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/resources/rgbscripts/.stamp"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/resources/rgbscripts"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/resources/rgbscripts/.stamp"
    DEPENDS ${SCRIPT_FILES_ABS} "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    COMMENT "Copying RGB scripts to the build tree for tests"
    VERBATIM
)
add_custom_target(copy_rgbscripts_for_tests ALL
    DEPENDS "${CMAKE_BINARY_DIR}/resources/rgbscripts/.stamp"
)

# Only perform the copy if we're building for Android
if(ANDROID)
    # Define the destination directory for the script files within the Android package
    set(SCRIPTS_ASSETS_DIR "${QT_ANDROID_PACKAGE_SOURCE_DIR}/${INSTALLROOT}/${RGBSCRIPTDIR}")

    # Create the scripts assets directory if it doesn't exist
    file(MAKE_DIRECTORY ${SCRIPTS_ASSETS_DIR})

    # Copy the script files to the assets directory
    foreach(SCRIPT_FILE IN LISTS SCRIPT_FILES)
        file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT_FILE} DESTINATION ${SCRIPTS_ASSETS_DIR})
    endforeach()
endif()

install(FILES ${SCRIPT_FILES} DESTINATION ${INSTALLROOT}/${RGBSCRIPTDIR})
