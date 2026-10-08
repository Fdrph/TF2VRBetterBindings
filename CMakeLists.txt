cmake_minimum_required(VERSION 3.24)
project(TF2VRBetterBindings VERSION 1.2.0 LANGUAGES CXX)

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
set(OPENXR_INCLUDE_DIR "${openxr_sdk_SOURCE_DIR}/include")

set(MOD_NAME "Nighteyes.TF2VRBetterBindings")
set(PACKAGE_NAME "Nighteyes-TF2VRBetterBindings-${PROJECT_VERSION}")
set(LAYER_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/manifest/XR_APILAYER_NIGHTEYES_tf2vr_better_bindings.json")
set(LAYER_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/config/TF2VRBetterBindings.ini")
set(COMMON_DEFINITIONS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
set(COMMON_OPTIONS /W4 /WX /permissive- /utf-8)

option(TF2VRBB_DEV_LOG "Write TF2VRBetterBindings.log next to the layer DLL (development builds only)" OFF)

set(LAYER_SOURCES
    src/layer/Layer.cpp
    src/layer/Engine.cpp
    src/layer/GameVar.cpp
    src/layer/Config.cpp
    src/layer/Log.cpp)

add_library(tf2vrbb_layer SHARED ${LAYER_SOURCES})
set_target_properties(tf2vrbb_layer PROPERTIES OUTPUT_NAME TF2VRBetterBindingsLayer)
target_include_directories(tf2vrbb_layer PRIVATE ${OPENXR_INCLUDE_DIR})
target_compile_definitions(tf2vrbb_layer PRIVATE ${COMMON_DEFINITIONS} XR_NO_PROTOTYPES $<$<BOOL:${TF2VRBB_DEV_LOG}>:TF2VRBB_LOGGING>)
target_compile_options(tf2vrbb_layer PRIVATE ${COMMON_OPTIONS})

add_library(tf2vrbb_plugin SHARED
    src/plugin/Plugin.cpp
    src/plugin/Registration.cpp)
set_target_properties(tf2vrbb_plugin PROPERTIES OUTPUT_NAME TF2VRBetterBindings)
target_compile_definitions(tf2vrbb_plugin PRIVATE ${COMMON_DEFINITIONS})
target_compile_options(tf2vrbb_plugin PRIVATE ${COMMON_OPTIONS})
target_link_libraries(tf2vrbb_plugin PRIVATE advapi32)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/LayerTests.cpp")
    add_library(tf2vrbb_layer_logging SHARED ${LAYER_SOURCES})
    set_target_properties(tf2vrbb_layer_logging PROPERTIES
        OUTPUT_NAME TF2VRBetterBindingsLayer
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/logging"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/logging"
        PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/logging")
    target_include_directories(tf2vrbb_layer_logging PRIVATE ${OPENXR_INCLUDE_DIR})
    target_compile_definitions(tf2vrbb_layer_logging PRIVATE ${COMMON_DEFINITIONS} XR_NO_PROTOTYPES TF2VRBB_LOGGING)
    target_compile_options(tf2vrbb_layer_logging PRIVATE ${COMMON_OPTIONS})

    add_library(fake_vstdlib SHARED tests/FakeVstdlib.cpp)
    set_target_properties(fake_vstdlib PROPERTIES OUTPUT_NAME vstdlib)
    target_compile_definitions(fake_vstdlib PRIVATE ${COMMON_DEFINITIONS})
    target_compile_options(fake_vstdlib PRIVATE ${COMMON_OPTIONS})

    add_executable(layer_tests tests/LayerTests.cpp)
    target_include_directories(layer_tests PRIVATE ${OPENXR_INCLUDE_DIR})
    target_compile_definitions(layer_tests PRIVATE ${COMMON_DEFINITIONS} XR_NO_PROTOTYPES)
    target_compile_options(layer_tests PRIVATE ${COMMON_OPTIONS})
    add_dependencies(layer_tests tf2vrbb_layer tf2vrbb_layer_logging fake_vstdlib)

    add_executable(plugin_tests tests/PluginTests.cpp src/plugin/Registration.cpp)
    target_include_directories(plugin_tests PRIVATE src/plugin)
    target_compile_definitions(plugin_tests PRIVATE ${COMMON_DEFINITIONS})
    target_compile_options(plugin_tests PRIVATE ${COMMON_OPTIONS})
    target_link_libraries(plugin_tests PRIVATE advapi32)
    add_dependencies(plugin_tests tf2vrbb_plugin)

    enable_testing()
    add_test(NAME layer_tests_logging COMMAND layer_tests "$<TARGET_FILE:tf2vrbb_layer_logging>" logging)
    add_test(NAME layer_tests_release COMMAND layer_tests "$<TARGET_FILE:tf2vrbb_layer>" $<IF:$<BOOL:${TF2VRBB_DEV_LOG}>,logging,release>)
    add_test(NAME plugin_tests COMMAND plugin_tests "$<TARGET_FILE:tf2vrbb_plugin>")
endif()

set(PACKAGE_ROOT "${CMAKE_BINARY_DIR}/package")
set(PACKAGE_DIR "${PACKAGE_ROOT}/${PACKAGE_NAME}")
set(PACKAGE_ZIP "${CMAKE_BINARY_DIR}/${PACKAGE_NAME}.zip")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/package/mods/${MOD_NAME}/mod.json.in" "${CMAKE_BINARY_DIR}/generated/mod.json" @ONLY)

add_custom_target(dist_package
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACKAGE_DIR}" "${PACKAGE_ZIP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/plugins/layer" "${PACKAGE_DIR}/mods/${MOD_NAME}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/package/mods/${MOD_NAME}/mod" "${PACKAGE_DIR}/mods/${MOD_NAME}/mod"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/generated/mod.json" "${PACKAGE_DIR}/mods/${MOD_NAME}/mod.json"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:tf2vrbb_plugin>" "${PACKAGE_DIR}/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:tf2vrbb_layer>" "${LAYER_MANIFEST}" "${LAYER_CONFIG}" "${PACKAGE_DIR}/plugins/layer/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${PACKAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/scripts/unregister.ps1" "${PACKAGE_DIR}/unregister-layer.ps1"
    COMMAND ${CMAKE_COMMAND} -E chdir "${PACKAGE_ROOT}" ${CMAKE_COMMAND} -E tar cf "${PACKAGE_ZIP}" --format=zip "${PACKAGE_NAME}"
    VERBATIM)
add_dependencies(dist_package tf2vrbb_layer tf2vrbb_plugin)

set(TF2VR_PACKAGES_DIR "" CACHE PATH "Optional: the TF2VR\\packages folder of a Titanfall 2 install, for the install_tf2vr target")
if(TF2VR_PACKAGES_DIR)
    add_custom_target(install_tf2vr
        COMMAND ${CMAKE_COMMAND} "-DSOURCE=${PACKAGE_DIR}" "-DDESTINATION=${TF2VR_PACKAGES_DIR}/${PACKAGE_NAME}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallPackage.cmake"
        VERBATIM)
    add_dependencies(install_tf2vr dist_package)
endif()
