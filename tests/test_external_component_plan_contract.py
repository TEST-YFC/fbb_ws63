"""Contract tests for the single, plan-driven external component hook."""

from pathlib import Path


ROOT = Path(__file__).parents[1]
BUILD_CORE = ROOT / "src/build/cmake/build_core.cmake"


def test_build_core_consumes_exact_cli_generated_cmake():
    text = BUILD_CORE.read_text(encoding="utf-8")

    assert "$ENV{FBB_COMPONENTS_CMAKE}" in text
    assert 'include("$ENV{FBB_COMPONENTS_CMAKE}")' in text
    assert "FBB_COMPONENTS_CMAKE was provided but does not exist" in text
    normalize = text.index(
        'file(TO_CMAKE_PATH "$ENV{FBB_PROJECT_DIR}" FBB_PROJECT_DIR)')
    include = text.index('include("$ENV{FBB_COMPONENTS_CMAKE}")')
    assert normalize < include
    main = text.index('add_subdirectory("${CMAKE_SOURCE_DIR}/main"')
    final_link = text.index(
        "target_link_libraries(\n"
        "            ${TARGET_NAME} PRIVATE "
        "${FBB_EXTERNAL_COMPONENT_TARGETS})")
    assert include < main < final_link


def test_v2_plan_precedes_legacy_out_of_tree_component_scan():
    text = BUILD_CORE.read_text(encoding="utf-8")

    plan_branch = text.index('if(NOT "$ENV{FBB_COMPONENTS_CMAKE}" STREQUAL "")')
    legacy_branch = text.index(
        'if("$ENV{FBB_COMPONENTS_CMAKE}" STREQUAL "")', plan_branch)
    scan = text.index("file(GLOB _proj_comps", legacy_branch)
    assert plan_branch < legacy_branch < scan
    assert text.index(
        'include("$ENV{FBB_COMPONENTS_CMAKE}")', plan_branch
    ) < text.index(
        'add_subdirectory("${CMAKE_SOURCE_DIR}/main"')


def test_v2_plan_hook_is_available_for_in_tree_and_out_of_tree_projects():
    """A locked market component uses the same exact hook in both layouts."""
    text = BUILD_CORE.read_text(encoding="utf-8")

    plan_branch = text.index(
        'if(NOT "$ENV{FBB_COMPONENTS_CMAKE}" STREQUAL "")')
    out_of_tree = text.index("if(FBB_OUT_OF_TREE)")
    include = text.index(
        'include("$ENV{FBB_COMPONENTS_CMAKE}")', plan_branch)

    assert plan_branch < out_of_tree
    assert include < out_of_tree


def test_build_core_registers_exact_generated_board_binding():
    text = BUILD_CORE.read_text(encoding="utf-8")

    assert '$ENV{FBB_BOARD_GENERATED_DIR}' in text
    assert "fbb_board_generated.c" in text
    assert "fbb_board_generated.h" in text
    assert "FBB_BOARD_GENERATED_DIR was provided but incomplete" in text
    assert "add_library(fbb_board STATIC" in text
    assert (
        '"${ROOT_DIR}/middleware/utils/fbb_board/core.c"'
        in text)
    assert (
        '"${ROOT_DIR}/middleware/utils/fbb_board/gpio.c"'
        in text)
    assert (
        '"${ROOT_DIR}/middleware/utils/fbb_board/fbb_board.c"'
        not in text)
    assert (
        "target_link_libraries(${TARGET_NAME} PRIVATE fbb_board)"
        in text)
    assert "list(APPEND RAM_COMPONENT fbb_board)" not in text
    assert (
        "target_link_libraries(fbb_board PRIVATE gpio chip_ws63)"
        in text)
    assert '"${ROOT_DIR}/drivers/chips/${CHIP}/include"' in text
    assert '"${ROOT_DIR}/middleware/utils/common_headers"' in text
    assert '"${ROOT_DIR}/drivers/drivers/hal/gpio"' in text
    assert '"${ROOT_DIR}/drivers/drivers/hal/gpio/v150"' in text
    assert '"${ROOT_DIR}/drivers/chips/${CHIP}/porting/gpio"' in text
    normalize = text.index(
        'file(TO_CMAKE_PATH "$ENV{FBB_BOARD_GENERATED_DIR}" '
        "FBB_BOARD_GENERATED_DIR)")
    source = text.index(
        '${FBB_BOARD_GENERATED_DIR}/src/fbb_board_generated.c')
    assert normalize < source


def test_board_runtime_separates_core_and_gpio_resource_logic():
    core = (
        ROOT / "src/middleware/utils/fbb_board/core.c"
    ).read_text(encoding="utf-8")
    gpio = (
        ROOT / "src/middleware/utils/fbb_board/gpio.c"
    ).read_text(encoding="utf-8")

    assert "fbb_board_api_version" in core
    assert "fbb_board_gpio(" not in core
    assert "fbb_gpio_write(" not in core
    assert "fbb_board_gpio(" in gpio
    assert "fbb_gpio_write(" in gpio


def test_plan_and_board_binding_are_isolated_to_requested_project_image():
    """A project application plan must not leak into flashboot/loader images."""
    text = BUILD_CORE.read_text(encoding="utf-8")

    guard = (
        'if("$ENV{FBB_PROJECT_TARGET}" STREQUAL "" OR '\
        '"$ENV{FBB_PROJECT_TARGET}" STREQUAL "${BIN_NAME}")')
    board_hook = text.index('if(NOT "$ENV{FBB_BOARD_GENERATED_DIR}" STREQUAL "")')
    component_hook = text.index('if(NOT "$ENV{FBB_COMPONENTS_CMAKE}" STREQUAL "")')
    guard_index = text.index(guard)

    assert guard_index < board_hook < component_hook
    assert text.index("endif() # FBB project image", component_hook) > component_hook
