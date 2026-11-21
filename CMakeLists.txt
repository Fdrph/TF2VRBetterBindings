cmake_minimum_required(VERSION 3.24)
project(TF2VRBetterBindings VERSION 1.2.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(openxr_sdk
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.1.63
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR headers_only)
FetchContent_MakeAvailable(openxr_sdk)

set(MOD_NAME "Nighteyes.TF2VRBetterBindings")
set(PACKAGE_NAME "Nighteyes-TF2VRBetterBindings-${PROJECT_VERSION}")
set(COMMON_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE XR_NO_PROTOTYPES)
set(COMMON_OPTIONS /W4 /WX /permissive- /utf-8)

option(TF2VRBB_DEV_LOG "Write TF2VRBetterBindings.log next to the layer DLL (development builds only)" OFF)

function(add_layer target logging)
    add_library(${target} SHARED src/layer/Layer.cpp)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME TF2VRBetterBindingsLayer)
    target_include_directories(${target} PRIVATE "${openxr_sdk_SOURCE_DIR}/include")
    target_compile_definitions(${target} PRIVATE ${COMMON_DEFINITIONS} $<$<BOOL:${logging}>:TF2VRBB_LOGGING>)
    target_compile_options(${target} PRIVATE ${COMMON_OPTIONS})
endfunction()

add_layer(tf2vrbb_layer ${TF2VRBB_DEV_LOG})

add_library(tf2vrbb_plugin SHARED src/plugin/Plugin.cpp src/plugin/Registration.cpp)
set_target_properties(tf2vrbb_plugin PROPERTIES OUTPUT_NAME TF2VRBetterBindings)
target_compile_definitions(tf2vrbb_plugin PRIVATE ${COMMON_DEFINITIONS})
target_compile_options(tf2vrbb_plugin PRIVATE ${COMMON_OPTIONS})

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/LayerTests.cpp")
    add_layer(tf2vrbb_layer_logging ON)
    set_target_properties(tf2vrbb_layer_logging PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/logging"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/logging" PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/logging")

    add_library(fake_vstdlib SHARED tests/FakeVstdlib.cpp)
    set_target_properties(fake_vstdlib PROPERTIES OUTPUT_NAME vstdlib)
    target_compile_options(fake_vstdlib PRIVATE ${COMMON_OPTIONS})

    add_executable(layer_tests tests/LayerTests.cpp)
    target_include_directories(layer_tests PRIVATE "${openxr_sdk_SOURCE_DIR}/include")
    target_compile_definitions(layer_tests PRIVATE ${COMMON_DEFINITIONS})
    target_compile_options(layer_tests PRIVATE ${COMMON_OPTIONS})
    add_dependencies(layer_tests tf2vrbb_layer tf2vrbb_layer_logging fake_vstdlib)
    set(GAME_EXE "$<TARGET_FILE_DIR:layer_tests>/Titanfall2VRLauncher.exe")
    add_custom_command(TARGET layer_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:layer_tests>" "${GAME_EXE}")

    add_executable(plugin_tests tests/PluginTests.cpp src/plugin/Registration.cpp)
    target_include_directories(plugin_tests PRIVATE src/plugin)
    target_compile_definitions(plugin_tests PRIVATE ${COMMON_DEFINITIONS})
    target_compile_options(plugin_tests PRIVATE ${COMMON_OPTIONS})
    add_dependencies(plugin_tests tf2vrbb_plugin)

    enable_testing()
    add_test(NAME layer_tests_passive COMMAND layer_tests "$<TARGET_FILE:tf2vrbb_layer>" passive)
    add_test(NAME layer_tests_logging COMMAND "${GAME_EXE}" "$<TARGET_FILE:tf2vrbb_layer_logging>" logging)
    add_test(NAME layer_tests_release COMMAND "${GAME_EXE}" "$<TARGET_FILE:tf2vrbb_layer>" $<IF:$<BOOL:${TF2VRBB_DEV_LOG}>,logging,release>)
    add_test(NAME plugin_tests COMMAND plugin_tests "$<TARGET_FILE:tf2vrbb_plugin>")
endif()

set(PACKAGE_ROOT "${CMAKE_BINARY_DIR}/package")
set(PACKAGE_DIR "${PACKAGE_ROOT}/${PACKAGE_NAME}")
set(PACKAGE_ZIP "${CMAKE_BINARY_DIR}/${PACKAGE_NAME}.zip")
configure_file("package/mods/${MOD_NAME}/mod.json.in" "${CMAKE_BINARY_DIR}/generated/mod.json" @ONLY)

add_custom_target(dist_package
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACKAGE_DIR}" "${PACKAGE_ZIP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/plugins/layer" "${PACKAGE_DIR}/mods/${MOD_NAME}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/package/mods/${MOD_NAME}/mod" "${PACKAGE_DIR}/mods/${MOD_NAME}/mod"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/generated/mod.json" "${PACKAGE_DIR}/mods/${MOD_NAME}/mod.json"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:tf2vrbb_plugin>" "${PACKAGE_DIR}/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:tf2vrbb_layer>" "${CMAKE_CURRENT_SOURCE_DIR}/manifest/XR_APILAYER_NIGHTEYES_tf2vr_better_bindings.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/TF2VRBetterBindings.ini" "${PACKAGE_DIR}/plugins/layer/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${PACKAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/scripts/unregister.ps1" "${PACKAGE_DIR}/unregister-layer.ps1"
    COMMAND ${CMAKE_COMMAND} -E chdir "${PACKAGE_ROOT}" ${CMAKE_COMMAND} -E tar cf "${PACKAGE_ZIP}" --format=zip "${PACKAGE_NAME}"
    VERBATIM)
add_dependencies(dist_package tf2vrbb_layer tf2vrbb_plugin)
