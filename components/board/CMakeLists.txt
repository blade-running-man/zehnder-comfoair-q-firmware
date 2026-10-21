# board_rgb_order.c is PURE -- no IDF, no pin -- and is here rather than in comfoair_led because
# the byte order is a property of the board, and the driver must not carry a second copy of it.
# test/test_board_rgb_order compiles this one file on the host.
idf_component_register(
    SRCS "board_m5stack_nanoc6.c" "board_rgb_order.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio
)
