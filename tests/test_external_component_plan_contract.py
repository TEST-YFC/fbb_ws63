"""Contract tests for the single, plan-driven external component hook."""

import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).parents[1]
BUILD_CORE = ROOT / "src/build/cmake/build_core.cmake"


def _configure_external_target(tmp_path, declaration, link_property):
    source_dir = tmp_path / "source"
    build_dir = tmp_path / "build"
    source_dir.mkdir()
    (source_dir / "component.c").write_text(
        "int external_component(void) { return 0; }\n", encoding="utf-8")
    build_core = BUILD_CORE.as_posix()
    cmake = f"""
cmake_minimum_required(VERSION 3.16)
project(fbb_external_target_contract C)
include("{build_core}")
add_library(fbb_sdk_interface INTERFACE)
{declaration}
_fbb_link_sdk_interfaces(external_component fbb_sdk_interface)
get_target_property(actual_links external_component {link_property})
if(NOT "fbb_sdk_interface" IN_LIST actual_links)
    message(FATAL_ERROR
        "SDK interface missing from ${{actual_links}}")
endif()
"""
    (source_dir / "CMakeLists.txt").write_text(cmake, encoding="utf-8")
    command = ["cmake", "-S", str(source_dir), "-B", str(build_dir)]
    if shutil.which("ninja"):
        command.extend(["-G", "Ninja"])
    result = subprocess.run(
        command,
        capture_output=True,
        check=False,
        text=True,
    )
    assert result.returncode == 0, result.stdout + result.stderr


def test_build_core_consumes_exact_cli_generated_cmake():
    text = BUILD_CORE.read_text(encoding="utf-8")

    assert "$ENV{FBB_COMPONENTS_CMAKE}" in text
    assert 'include("$ENV{FBB_COMPONENTS_CMAKE}")' in text
    assert "FBB_COMPONENTS_CMAKE was provided but does not exist" in text
    normalize = text.index(
        'file(TO_CMAKE_PATH "$ENV{FBB_PROJECT_DIR}" FBB_PROJECT_DIR)')
    include = text.index('include("$ENV{FBB_COMPONENTS_CMAKE}")')
    main = text.index('add_subdirectory("${CMAKE_SOURCE_DIR}/main"')
    final_link = text.index(
        "target_link_libraries(\n"
        "            ${TARGET_NAME} PRIVATE "
        "${FBB_EXTERNAL_COMPONENT_TARGETS})")
    assert normalize < include < main < final_link


def test_sdk_tree_exists_before_external_plan_is_loaded():
    """Generated external targets may consume the SDK's public interfaces."""
    text = BUILD_CORE.read_text(encoding="utf-8")

    sdk_tree = text.index(
        "foreach(_comp application bt bootloader kernel drivers middleware")
    include = text.index('include("$ENV{FBB_COMPONENTS_CMAKE}")')
    assert sdk_tree < include


def test_external_targets_inherit_existing_sdk_public_interfaces():
    """Reuse build_component's compile contract without relinking drivers."""
    text = BUILD_CORE.read_text(encoding="utf-8")

    include = text.index('include("$ENV{FBB_COMPONENTS_CMAKE}")')
    inherit = text.index(
        '"${_fbb_external_target}" ${_fbb_sdk_interfaces})')
    final_link = text.index(
        "${TARGET_NAME} PRIVATE ${FBB_EXTERNAL_COMPONENT_TARGETS})")
    assert include < inherit < final_link
    assert "gpio" not in text[include:final_link]
    assert "pinctrl" not in text[include:final_link]


def test_source_component_inherits_sdk_interfaces(tmp_path):
    _configure_external_target(
        tmp_path,
        "add_library(external_component STATIC component.c)",
        "LINK_LIBRARIES",
    )


def test_header_only_component_inherits_sdk_interfaces(tmp_path):
    _configure_external_target(
        tmp_path,
        "add_library(external_component INTERFACE)",
        "INTERFACE_LINK_LIBRARIES",
    )


def test_prebuilt_component_inherits_sdk_interfaces(tmp_path):
    _configure_external_target(
        tmp_path,
        "add_library(external_component STATIC IMPORTED GLOBAL)",
        "INTERFACE_LINK_LIBRARIES",
    )


def test_v2_plan_precedes_legacy_out_of_tree_component_scan():
    text = BUILD_CORE.read_text(encoding="utf-8")

    plan_branch = text.index(
        'if(NOT "$ENV{FBB_COMPONENTS_CMAKE}" STREQUAL "")')
    legacy_branch = text.index(
        'if("$ENV{FBB_COMPONENTS_CMAKE}" STREQUAL "")', plan_branch)
    scan = text.index("file(GLOB _proj_comps", legacy_branch)
    assert plan_branch < legacy_branch < scan
    assert text.index(
        'include("$ENV{FBB_COMPONENTS_CMAKE}")', plan_branch
    ) < text.index(
        'add_subdirectory("${CMAKE_SOURCE_DIR}/main"')


def test_project_main_consumes_external_component_public_interfaces():
    text = BUILD_CORE.read_text(encoding="utf-8")

    main = text.index('add_subdirectory("${CMAKE_SOURCE_DIR}/main"')
    main_link = text.index(
        "${FBB_PROJECT_COMPONENT_NAME} PRIVATE\n"
        "                    ${FBB_EXTERNAL_COMPONENT_TARGETS})")
    final_link = text.index(
        "${TARGET_NAME} PRIVATE ${FBB_EXTERNAL_COMPONENT_TARGETS})")
    assert main < main_link < final_link


def test_plan_is_isolated_to_requested_project_image():
    """An application plan must not leak into flashboot or loader images."""
    text = BUILD_CORE.read_text(encoding="utf-8")

    guard = (
        'if("$ENV{FBB_PROJECT_TARGET}" STREQUAL "" OR '
        '"$ENV{FBB_PROJECT_TARGET}" STREQUAL "${BIN_NAME}")')
    component_hook = text.index(
        'if(NOT "$ENV{FBB_COMPONENTS_CMAKE}" STREQUAL "")')
    guard_index = text.index(guard)

    assert guard_index < component_hook
    assert text.index(
        "endif() # FBB project image", component_hook) > component_hook


def test_sdk_contains_no_board_runtime_or_sdk_port_copy():
    """Board API, runtime and HiSilicon UAPI calls belong to the market asset."""
    text = BUILD_CORE.read_text(encoding="utf-8")

    assert "FBB_BOARD_GENERATED_DIR" not in text
    assert "fbb_board_sdk_port" not in text
    assert "fbb_register_image_extension" not in text
    assert "FBB_IMAGE_EXTENSION_TARGETS" not in text
    assert not (ROOT / "src/include/fbb/board.h").exists()
    assert not (ROOT / "src/include/fbb/board_port.h").exists()
    board_dir = ROOT / "src/middleware/utils/fbb_board"
    assert not board_dir.exists() or not any(board_dir.iterdir())
