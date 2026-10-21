# Pure: the GET /api/system document, the unit's error-text table and its one-line summary.
# REQUIRES comfoair_probe_report for the one version-word decoder, comfoair_probe_version_text().
idf_component_register(
    SRCS "comfoair_sysinfo.cpp" "comfoair_sysinfo_errors.cpp" "comfoair_sysinfo_summary.cpp"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_probe_report
)
