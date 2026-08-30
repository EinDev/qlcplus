project(profiles)

file(GLOB PROFILE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.qxi")

# engine/test/common/resource_paths.h's INTERNAL_PROFILEDIR is a path relative
# to a test binary's own directory (see engine/test/CMakeLists.txt, which sets
# WORKING_DIRECTORY to each test's $<TARGET_FILE_DIR>), resolving to
# ${CMAKE_BINARY_DIR}/resources/inputprofiles - so inputoutputmap_test (and
# any other suite using INTERNAL_PROFILEDIR) needs a live copy of this whole
# directory there, refreshed whenever a profile changes. Nothing did this for
# a normal desktop build (only the ANDROID branch below copied anything), so
# InputOutputMap_Test::loadInputProfiles() found zero profiles. Mirrors the
# equivalent fix in resources/rgbscripts/CMakeLists.txt.
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/resources/inputprofiles/.stamp"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/resources/inputprofiles"
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/resources/inputprofiles/.stamp"
    DEPENDS ${PROFILE_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    COMMENT "Copying input profiles to the build tree for tests"
    VERBATIM
)
add_custom_target(copy_inputprofiles_for_tests ALL
    DEPENDS "${CMAKE_BINARY_DIR}/resources/inputprofiles/.stamp"
)

# Only perform the copy if we're building for Android
if(ANDROID)
    # Define the destination directory for the profile files within the Android package
    set(PROFILES_ASSETS_DIR "${QT_ANDROID_PACKAGE_SOURCE_DIR}/${INPUTPROFILEDIR}")

    # Create the profiles assets directory if it doesn't exist
    file(MAKE_DIRECTORY ${PROFILES_ASSETS_DIR})

    # Copy the profile .qxi files to the assets directory
    foreach(PROFILE_FILE IN LISTS PROFILE_FILES)
        file(COPY ${PROFILE_FILE} DESTINATION ${PROFILES_ASSETS_DIR})
    endforeach()
endif()

install(FILES ${PROFILE_FILES} DESTINATION ${INSTALLROOT}/${INPUTPROFILEDIR})
