project(fixtures)

# Install FixturesMap.xml and all subdirectories (except ./scripts and .)
file(GLOB FIXTURES_FILES_AND_DIRECTORIES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(FILTER FIXTURES_FILES_AND_DIRECTORIES EXCLUDE REGEX "^scripts$")
list(REMOVE_ITEM FIXTURES_FILES_AND_DIRECTORIES ".")

# engine/test/common/resource_paths.h's INTERNAL_FIXTUREDIR is a path relative
# to a test binary's own directory (see engine/test/CMakeLists.txt, which sets
# WORKING_DIRECTORY to each test's $<TARGET_FILE_DIR>), resolving to
# ${CMAKE_BINARY_DIR}/resources/fixtures - so qlcfixturedefcache_test needs a
# live copy of this whole directory (including FixturesMap.xml) there,
# refreshed whenever a fixture definition changes, independent of the
# Android-only asset copy below. Same gap as resources/rgbscripts/CMakeLists.txt
# fixed for rgbscript_test - see that file's comment for the full story.
file(GLOB_RECURSE FIXTURE_FILES_ABS "${CMAKE_CURRENT_SOURCE_DIR}/*")
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/resources/fixtures/.stamp"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/resources/fixtures"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/resources/fixtures/.stamp"
    DEPENDS ${FIXTURE_FILES_ABS}
    COMMENT "Copying fixture definitions to the build tree for tests"
    VERBATIM
)
add_custom_target(copy_fixtures_for_tests ALL
    DEPENDS "${CMAKE_BINARY_DIR}/resources/fixtures/.stamp"
)

# Only perform the copy if we're building for Android
if(ANDROID)
    # Loop through each file and directory and copy them to the assets directory, excluding the scripts directory
    foreach(FIXTURE_FILE_AND_DIRECTORY ${FIXTURES_FILES_AND_DIRECTORIES})
        if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${FIXTURE_FILE_AND_DIRECTORY}")
            # Calculate the full path to the source directory
            set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${FIXTURE_FILE_AND_DIRECTORY}")

            # Calculate the full path to the destination directory within the Android package
            set(DESTINATION_DIR "${QT_ANDROID_PACKAGE_SOURCE_DIR}/${FIXTUREDIR}")

            # Copy the directory to the assets directory
            file(COPY ${SOURCE_DIR} DESTINATION ${DESTINATION_DIR})
        endif()
    endforeach()

    # Copy the FixturesMap.xml file to the assets directory
    set(SOURCE_FIXTURES_MAP "${CMAKE_CURRENT_SOURCE_DIR}/FixturesMap.xml")
    set(DESTINATION_FIXTURES_MAP "${QT_ANDROID_PACKAGE_SOURCE_DIR}/${FIXTUREDIR}")
    file(COPY ${SOURCE_FIXTURES_MAP} DESTINATION ${DESTINATION_FIXTURES_MAP})
endif()

foreach(FIXTURE_FILE_AND_DIRECTORY ${FIXTURES_FILES_AND_DIRECTORIES})
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${FIXTURE_FILE_AND_DIRECTORY}")
        install(DIRECTORY ${FIXTURE_FILE_AND_DIRECTORY} DESTINATION ${INSTALLROOT}/${FIXTUREDIR})
    endif()
endforeach()

install(FILES FixturesMap.xml DESTINATION ${INSTALLROOT}/${FIXTUREDIR})
