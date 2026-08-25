add_executable(treeflatmodel_test WIN32
    treeflatmodel_test.cpp treeflatmodel_test.h
    ../../treeflatmodel.cpp ../../treeflatmodel.h
    ../../treemodel.cpp ../../treemodel.h
    ../../treemodelitem.cpp ../../treemodelitem.h
)

target_include_directories(treeflatmodel_test PRIVATE
    ../..
)

target_link_libraries(treeflatmodel_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Qml
    Qt${QT_MAJOR_VERSION}::Test
)
