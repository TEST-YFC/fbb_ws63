# The cross-SDK fbb-board runtime is a normal market component. This target is
# only its WS63 native Port and is linked by the generated BaseBuildPlan when
# the selected component provides fbb.board-runtime.
add_library(fbb_board_sdk_port STATIC
    "${CMAKE_CURRENT_LIST_DIR}/platform.c")
set_target_properties(fbb_board_sdk_port PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_include_directories(fbb_board_sdk_port
    PUBLIC "${ROOT_DIR}/include"
    PRIVATE
        "${ROOT_DIR}/include/driver"
        "${ROOT_DIR}/drivers/chips/${CHIP}/include"
        "${ROOT_DIR}/drivers/chips/${CHIP}/porting/pinctrl"
        "${ROOT_DIR}/middleware/utils/hwsec_c/include"
        "${ROOT_DIR}/middleware/utils/common_headers"
        "${ROOT_DIR}/drivers/drivers/hal/gpio"
        "${ROOT_DIR}/drivers/drivers/hal/gpio/v150"
        "${ROOT_DIR}/drivers/chips/${CHIP}/porting/gpio")
target_link_libraries(fbb_board_sdk_port PRIVATE gpio chip_ws63)
target_compile_definitions(fbb_board_sdk_port PRIVATE ${DEFINES})
target_compile_options(fbb_board_sdk_port PRIVATE ${CCFLAGS}
    $<$<BOOL:${USE_KCONFIG}>:
    -include${PROJECT_BINARY_DIR}/mconfig.h>)
