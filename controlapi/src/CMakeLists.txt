set(module_name "qlcplusapiserver")

set(CONTROLAPI_SOURCES
    apienvelope.cpp apienvelope.h
    apisession.cpp apisession.h
    apidispatcher.cpp apidispatcher.h
    apiserver.cpp apiserver.h
    domains/apiiodomain.cpp domains/apiiodomain.h
    domains/apicoredomain.cpp domains/apicoredomain.h
)

# Static, linked directly into the qlcplus5 executable - same reasoning as
# qlcpluswebaccess (see webaccess/src/CMakeLists.txt): nothing to install,
# nothing that could clash with anything else.
add_library(${module_name} STATIC ${CONTROLAPI_SOURCES})

target_include_directories(${module_name} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ../../engine/src
)

find_package(Qt${QT_MAJOR_VERSION} REQUIRED COMPONENTS WebSockets)

# Deliberately minimal: no Quick/Widgets/Multimedia/qmlui dependency. This
# module must build and run without qmlui (or any UI) present at all -
# ApiCoreDomain reaches project-lifecycle operations (new/open/save
# workspace, recent files, working path) through the plain ApiProjectHost
# interface (apiprojecthost.h), which qmlui's App implements, rather than
# through App/QQuickView directly.
target_link_libraries(${module_name} PUBLIC
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::WebSockets
    qlcplusengine
)
