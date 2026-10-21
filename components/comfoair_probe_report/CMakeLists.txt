# Pure: the meaning of the probe's bytes and their JSON. Apart from comfoair_probe so that the
# decoders and the catalogue are tested against hand-built results, not only through a walk.
idf_component_register(
    SRCS "comfoair_probe_catalogue.cpp" "comfoair_probe_decode.cpp" "comfoair_probe_report.cpp"
         "comfoair_probe_report_pdos.cpp"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_probe
)
