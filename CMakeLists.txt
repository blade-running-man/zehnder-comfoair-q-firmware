cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The version must be set BEFORE project(): that is the only point at which IDF takes
# PROJECT_VER as given instead of running its own git describe. PYTHON is the interpreter
# the build itself runs (PlatformIO and idf.py both pass -DPYTHON=); IDF stores it at include.
include(${CMAKE_CURRENT_LIST_DIR}/tools/version.cmake)
idf_build_get_property(_comfoair_python PYTHON)
comfoair_project_ver(PROJECT_VER "${_comfoair_python}")
project(comfoair-q)
