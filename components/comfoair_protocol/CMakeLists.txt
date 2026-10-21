# Header-only, and that is the point: these headers include nothing but the C++ standard
# library, which is what lets test/ compile the exact same code the firmware runs (AGENTS.md).
# DO NOT include an ESP-IDF header here: the host suites would stop compiling this code.
idf_component_register(INCLUDE_DIRS "include")
