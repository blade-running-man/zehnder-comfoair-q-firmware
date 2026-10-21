# PROJECT_VER from tools/version.py, the one source of the firmware version (SemVer from the v*
# tag; the derivation table is the contract in tools/version.py's docstring).
#
# Why not ESP-IDF's own default: without PROJECT_VER, IDF stamps `git describe --always --tags
# --dirty`, which is not SemVer ("v0.1.0-3-gabc1234", or a bare "cda52b6" with no tag), and
# which an unpacked archive inside another checkout takes from THAT checkout. Setting
# PROJECT_VER before project() is IDF's documented override (tools/cmake/project.cmake: "PROJECT_VER
# has been set before calling project()"), so esp_app_desc_t.version is exactly this string.

set(_COMFOAIR_VERSION_PY "${CMAKE_CURRENT_LIST_DIR}/version.py")

# comfoair_project_ver(<out-var> <python>)
#
# Sets <out-var> in the caller's scope, writes it to ${CMAKE_BINARY_DIR}/project_ver.txt (the
# stamp tools/pio_version.py compares against) and makes CMake re-configure when HEAD, the
# current branch, the tags or packed-refs change -- the same files-as-dependencies trick IDF's
# GetGitRevisionDescription uses for HEAD and the branch ref, extended to refs/tags because IDF
# never re-configures on a new tag. The paths come from version.py, which resolves a linked
# worktree's .git FILE to its private git dir (HEAD) and the common dir (refs).
#
# A failing script fails the configure. DO NOT fall back to a default version: a firmware that
# says "1" or "0.0.0" when its tag is malformed is the silent drift this whole scheme removes.
function(comfoair_project_ver out python)
    execute_process(
        COMMAND "${python}" "${_COMFOAIR_VERSION_PY}" --cmake
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE lines
        ERROR_VARIABLE err
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "tools/version.py failed (exit ${rc}) with '${python}':\n${err}")
    endif()
    string(REPLACE "\n" ";" lines "${lines}")
    list(GET lines 0 version)
    list(REMOVE_AT lines 0)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_COMFOAIR_VERSION_PY}" ${lines})
    file(WRITE "${CMAKE_BINARY_DIR}/project_ver.txt" "${version}\n")
    message(STATUS "Firmware version (tools/version.py): ${version}")
    set(${out} "${version}" PARENT_SCOPE)
endfunction()
