add_executable(treemodel_test WIN32
    treemodel_test.cpp treemodel_test.h
    ../../treemodel.cpp ../../treemodel.h
    ../../treemodelitem.cpp ../../treemodelitem.h
    ../../treeflatmodel.cpp ../../treeflatmodel.h
)

target_include_directories(treemodel_test PRIVATE
    ../..
)

target_link_libraries(treemodel_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Qml
    Qt${QT_MAJOR_VERSION}::Test
)
