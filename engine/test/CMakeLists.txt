project(test)

add_subdirectory(beattracker)
add_subdirectory(beattrackerbench)
add_subdirectory(bus)
add_subdirectory(channelsgroup)
add_subdirectory(channelmodifier)
add_subdirectory(chaser)
add_subdirectory(chaserrunner)
add_subdirectory(chaserstep)
add_subdirectory(collection)
add_subdirectory(cue)
add_subdirectory(cuestack)
add_subdirectory(dmxdumpfactoryproperties)
add_subdirectory(doc)
add_subdirectory(efx)
add_subdirectory(efxfixture)
add_subdirectory(fadechannel)
add_subdirectory(fixture)
add_subdirectory(fixturegroup)
add_subdirectory(fixtureremapper)
add_subdirectory(function)
add_subdirectory(genericdmxsource)
add_subdirectory(genericfader)
add_subdirectory(grandmaster)
add_subdirectory(inputoutputmap)
add_subdirectory(inputpatch)
add_subdirectory(keypadparser)
add_subdirectory(mastertimer)
add_subdirectory(monitorproperties)
add_subdirectory(outputpatch)
add_subdirectory(qlccapability)
add_subdirectory(qlcchannel)
add_subdirectory(qlcclipboard)
add_subdirectory(qlcfile)
add_subdirectory(qlcfixturedef)
add_subdirectory(qlcfixturedefcache)
add_subdirectory(qlcfixturehead)
add_subdirectory(qlcfixturemode)
add_subdirectory(qlci18n)
add_subdirectory(qlcinputchannel)
add_subdirectory(qlcinputprofile)
add_subdirectory(qlcmacros)
add_subdirectory(qlcmodifierscache)
add_subdirectory(qlcpalette)
add_subdirectory(qlcphysical)
add_subdirectory(qlcpoint)
add_subdirectory(rgbalgorithm)
add_subdirectory(rgbmatrix)
add_subdirectory(rgbplain)
add_subdirectory(rgbscript)
add_subdirectory(rgbtext)
add_subdirectory(scene)
add_subdirectory(scenegrandmaster)
add_subdirectory(scenevalue)
add_subdirectory(script)
add_subdirectory(sequence)
add_subdirectory(show)
add_subdirectory(showfunction)
add_subdirectory(showrunner)
add_subdirectory(track)
add_subdirectory(universe)
add_subdirectory(universeperf)
add_subdirectory(video)
add_subdirectory(iopluginstub)

# Register every QTest suite above (but not beattrackerbench, a benchmark with
# no pass/fail assertions, or iopluginstub, a stub library) with CTest, so the
# whole suite runs via a single `ctest` invocation. Each binary resolves its
# fixture/profile/script resources via paths relative to its own binary dir
# (see common/resource_paths.h), hence WORKING_DIRECTORY per test.
set(QLCPLUS_ENGINE_TESTS
    beattracker_test
    bus_test
    channelmodifier_test
    channelsgroup_test
    chaser_test
    chaserrunner_test
    chaserstep_test
    collection_test
    cue_test
    cuestack_test
    dmxdumpfactoryproperties_test
    doc_test
    efx_test
    efxfixture_test
    fadechannel_test
    fixture_test
    fixturegroup_test
    fixtureremapper_test
    function_test
    genericdmxsource_test
    genericfader_test
    grandmaster_test
    inputoutputmap_test
    inputpatch_test
    keypadparser_test
    mastertimer_test
    monitorproperties_test
    outputpatch_test
    qlccapability_test
    qlcchannel_test
    qlcclipboard_test
    qlcfile_test
    qlcfixturedef_test
    qlcfixturedefcache_test
    qlcfixturehead_test
    qlcfixturemode_test
    qlci18n_test
    qlcinputchannel_test
    qlcinputprofile_test
    qlcmacros_test
    qlcmodifierscache_test
    qlcpalette_test
    qlcphysical_test
    qlcpoint_test
    rgbalgorithm_test
    rgbmatrix_test
    rgbplain_test
    rgbscript_test
    rgbtext_test
    scene_test
    scenegrandmaster_test
    scenevalue_test
    script_test
    sequence_test
    show_test
    showfunction_test
    showrunner_test
    track_test
    universe_test
    universeperf_test
    video_test
)
foreach(_qlcplus_test_target ${QLCPLUS_ENGINE_TESTS})
    add_test(NAME ${_qlcplus_test_target}
             COMMAND $<TARGET_FILE:${_qlcplus_test_target}>
             WORKING_DIRECTORY $<TARGET_FILE_DIR:${_qlcplus_test_target}>)
endforeach()

# Convenience aggregate target: `cmake --build build --target engine_tests`
# builds just the test binaries (and whatever engine/plugin code they need),
# without doing a full project build.
add_custom_target(engine_tests DEPENDS ${QLCPLUS_ENGINE_TESTS})
