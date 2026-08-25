add_executable(qlcfile_test WIN32
    qlcfile_test.cpp qlcfile_test.h
)
target_include_directories(qlcfile_test PRIVATE
    ../../../plugins/interfaces
    ../../src
)

target_link_libraries(qlcfile_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusengine
)

# XMLReader() opens these by their bare relative name, so they need to sit next
# to the built binary (previously only done by the root unittest.sh, which
# doesn't run on this Windows/CTest setup).
foreach(_qlcfile_test_datafile broken.xml readonly.xml.in)
    configure_file(${_qlcfile_test_datafile} ${_qlcfile_test_datafile} COPYONLY)
endforeach()

# Consider using qt_generate_deploy_app_script() for app deployment if
# the project can use Qt 6.3. In that case rerun qmake2cmake with
# --min-qt-version=6.3.
