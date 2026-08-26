add_executable(arrangegeometry_test WIN32
    arrangegeometry_test.cpp arrangegeometry_test.h
)

target_link_libraries(arrangegeometry_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
)
