project(webui)

# Browser-based QLC+ 5 web UI. Everything in this directory (index.html plus
# any nested api/, assets/, vendor/, ... folders) is plain static content read
# from disk at runtime by controlapi's WebServer (controlapi/src/webserver.cpp)
# when qlcplus5 is started with --webui - nothing here is compiled or embedded
# into the executable. See docs/webui.md for the flags and the dev workflow.
#
# Installed next to the other data directories (Plugins/, Meshes/, Web/ on
# Windows; share/qlcplus/webui on Linux) - WEBUIDIR comes from variables.cmake.
# The trailing slash on the source path installs the *contents* of this
# directory, not a "webui" folder inside WEBUIDIR. CMakeLists.txt itself,
# dotfiles and a local node_modules/ (if anyone ever adds a tooling step here)
# are build-time only and never shipped.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
        DESTINATION ${INSTALLROOT}/${WEBUIDIR}
        PATTERN "CMakeLists.txt" EXCLUDE
        PATTERN "node_modules" EXCLUDE
        REGEX "/\\.[^/]*$" EXCLUDE)
