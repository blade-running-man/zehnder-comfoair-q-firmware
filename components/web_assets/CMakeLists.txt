# The web UI is part of the firmware build, not a step someone has to remember.
# AGENTS.md forbids a manual "don't forget to rebuild the front end".
#
# Why the SPA is built at CONFIGURE time and not as a build rule: EMBED_FILES is
# resolved by idf_component_register while CMake configures, so the three .gz
# files have to exist by then. The custom target below re-runs the build on every
# `pio run`, and the generated .S rules depend on the files, so an edit under
# web/src is picked up on the next build.
#
# Three files, ~120 KB gzipped. EMBED_FILES and not a LittleFS partition for that
# reason and for a better one: the UI and the firmware become a single artefact,
# so an OTA cannot leave a new binary serving an old UI.

set(WEB_DIR ${CMAKE_CURRENT_LIST_DIR}/../../web)
set(WEB_DIST ${WEB_DIR}/dist)

find_program(NPM_EXECUTABLE npm)
if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR
        "npm not found. The web UI is compiled into the firmware, so building "
        "this project needs Node. See README.md.")
endif()

if(NOT EXISTS ${WEB_DIR}/node_modules)
    message(STATUS "web: installing dependencies")
    execute_process(COMMAND ${NPM_EXECUTABLE} ci --no-fund --no-audit
                    WORKING_DIRECTORY ${WEB_DIR}
                    RESULT_VARIABLE web_install_result)
    if(NOT web_install_result EQUAL 0)
        message(FATAL_ERROR "web: npm ci failed (${web_install_result})")
    endif()
endif()

message(STATUS "web: building SPA")
execute_process(COMMAND ${NPM_EXECUTABLE} run build
                WORKING_DIRECTORY ${WEB_DIR}
                RESULT_VARIABLE web_build_result)
if(NOT web_build_result EQUAL 0)
    message(FATAL_ERROR "web: build failed (${web_build_result})")
endif()

idf_component_register(
    SRCS "web_assets.c"
    INCLUDE_DIRS "include"
    EMBED_FILES
        "${WEB_DIST}/index.html.gz"
        "${WEB_DIST}/app.js.gz"
        "${WEB_DIST}/style.css.gz"
)

add_custom_target(web_spa ALL
    COMMAND ${NPM_EXECUTABLE} run build
    WORKING_DIRECTORY ${WEB_DIR}
    COMMENT "Rebuilding the web UI")
add_dependencies(${COMPONENT_LIB} web_spa)
