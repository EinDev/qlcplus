add_executable(showgroupmove_test WIN32
    showgroupmove_test.cpp showgroupmove_test.h
    ../../../engine/src/showmovehelper.cpp ../../../engine/src/showmovehelper.h
)

# ShowMoveHelper lives in engine/src (it is shared by the Show Manager and
# the Control API's functions.show.* domain) but is pure Qt Core, so it is
# compiled straight into this test rather than linking the whole engine.
target_include_directories(showgroupmove_test PRIVATE
    ../..
    ../../../engine/src
)

target_link_libraries(showgroupmove_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Test
)
