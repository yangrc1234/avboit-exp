include(../donut/compileshaders.cmake)
donut_compile_shaders_all_platforms(TARGET avboit_shaders PROJECT_NAME AVBOIT
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/shaders.cfg FOLDER AVBOIT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/AdaptiveDepth.hlsl
    SHADERMAKE_OPTIONS_DXIL --embedPDB
    OUTPUT_BASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/avboit)


add_library(avboit_render STATIC
    render/ZeroTransmittance.cpp
    render/InterfacePass.cpp
    render/ExtinctionVolume.cpp
    render/OitResolve.cpp
    render/GaussianPyramid.cpp
    render/FrostComposition.cpp
    render/DistortionPass.cpp
    render/DistortionComposite.cpp
    render/TransparencyPipeline.cpp
)
target_link_libraries(avboit_render PUBLIC nvrhi)

add_executable(avboit_viewer sample/NativeViewer.cpp sample/DonutSceneHost.cpp sample/ViewerOptions.cpp sample/MaterialTable.cpp sample/SubmissionOrder.cpp sample/SceneLoader.cpp debug/GpuProfiler.cpp sample/FixtureGeometry.cpp sample/StressFixtures.cpp sample/ViewerUi.cpp)
target_link_libraries(avboit_viewer avboit_render donut_app donut_engine donut_render cgltf)
target_sources(avboit_viewer PRIVATE debug/StablePowerState.cpp)
target_link_libraries(avboit_viewer advapi32)
add_dependencies(avboit_viewer avboit_shaders)


target_include_directories(avboit_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTING)
    donut_compile_shaders_all_platforms(TARGET avboit_test_shaders PROJECT_NAME AVBOIT_TESTS
        CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/test_shaders.cfg FOLDER AVBOIT
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/AdaptiveDepth.hlsl
        SHADERMAKE_OPTIONS_DXIL --embedPDB
        OUTPUT_BASE ${CMAKE_BINARY_DIR}/tests/bin/shaders/avboit)
    add_executable(avboit_gpu_test tests/AdaptiveDepthGpuTest.cpp tests/BoundsOccupancyTest.cpp tests/ExtinctionTest.cpp tests/BlurDispatchTest.cpp tests/BackgroundSamplingTest.cpp)
    target_link_libraries(avboit_gpu_test donut_app donut_engine)
    add_dependencies(avboit_gpu_test avboit_test_shaders)
    add_test(NAME adaptive_depth_gpu COMMAND avboit_gpu_test -dx12)
    add_test(NAME native_raster_smoke COMMAND avboit_viewer --headless)

    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/gltf-sample-assets/Models/Sponza/glTF/Sponza.gltf")
        add_test(NAME native_sponza_smoke COMMAND avboit_viewer --headless --sponza-path "${CMAKE_SOURCE_DIR}/assets/gltf-sample-assets/Models/Sponza/glTF/Sponza.gltf")
        add_test(NAME native_donut_scene COMMAND avboit_viewer --headless --donut-scene --sponza-path "${CMAKE_SOURCE_DIR}/assets/gltf-sample-assets/Models/Sponza/glTF/Sponza.gltf" --stress 10 --emissive-balls)
        add_test(NAME native_donut_vfx COMMAND avboit_viewer --headless --donut-scene --sponza-path "${CMAKE_SOURCE_DIR}/assets/gltf-sample-assets/Models/Sponza/glTF/Sponza.gltf" --stress 8 --vfx-mode 3 --time 0.15)
    endif()

    add_test(NAME native_imgui_controls COMMAND avboit_viewer --ui-self-test --glass-sphere --no-smoke)
    add_test(NAME native_sparse_frost COMMAND avboit_viewer --headless --sparse-blur --emissive-balls --no-smoke)
    add_test(NAME native_crossing_surfaces COMMAND avboit_viewer --headless --stress 4 --time 2)
    add_test(NAME native_perforated_interface COMMAND avboit_viewer --headless --stress 10 --emissive-balls)
    add_test(NAME native_normal_refraction COMMAND avboit_viewer --headless --glass-sphere --normal-sphere --sphere-roughness 0.6 --refraction-gain 4)
    add_executable(avboit_options_test tests/ViewerOptionsTest.cpp sample/ViewerOptions.cpp)
    add_test(NAME viewer_options COMMAND avboit_options_test)
    add_executable(avboit_material_test tests/MaterialTableTest.cpp sample/MaterialTable.cpp)
    add_test(NAME material_table COMMAND avboit_material_test)
    add_executable(avboit_order_test tests/SubmissionOrderTest.cpp sample/SubmissionOrder.cpp)
    add_test(NAME submission_order COMMAND avboit_order_test)
    add_executable(avboit_interface_test tests/InterfaceSelectionTest.cpp sample/MaterialTable.cpp sample/SubmissionOrder.cpp)
    add_test(NAME interface_selection COMMAND avboit_interface_test)
    add_test(NAME native_half_accumulation COMMAND avboit_viewer --headless --stress 6 --accum-half --submission-order 1)
    add_test(NAME native_half_composition COMMAND avboit_viewer --headless --stress 6 --composition-half --submission-order 2)
    add_test(NAME native_half_precision COMMAND avboit_viewer --headless --stress 6 --accum-half --composition-half --submission-order 2)
    add_test(NAME native_heat_wave COMMAND avboit_viewer --headless --vfx-mode 1 --time 0.7)
    add_test(NAME native_dual_shock COMMAND avboit_viewer --headless --vfx-mode 3 --stress 8 --time 0.15)
    add_test(NAME viewer_help COMMAND avboit_viewer --help)
    add_test(NAME native_half_extinction COMMAND avboit_viewer --headless --volume-scale 2 --stress 4 --time 2)
    add_test(NAME native_eighth_extinction COMMAND avboit_viewer --headless --volume-scale 8 --stress 10 --poison-extinction)

    add_test(NAME native_hardware_depth COMMAND avboit_viewer --headless --hardware-depth --stress 6)

    add_executable(avboit_camera_test tests/SampleCameraTest.cpp)
    target_link_libraries(avboit_camera_test donut_app)
    add_test(NAME sample_camera COMMAND avboit_camera_test)

    add_test(NAME native_render_size_small COMMAND avboit_viewer --headless --stress 5 --render-width 512 --render-height 320 --volume-scale 8)
    add_test(NAME native_render_size_large COMMAND avboit_viewer --headless --stress 10 --render-width 1280 --render-height 768 --sparse-blur)

    add_test(NAME native_window_follow COMMAND avboit_viewer --ui-self-test --glass-sphere --no-smoke --follow-window)

    add_test(NAME native_fixed_z_16 COMMAND avboit_viewer --headless --fixed-z --depth-budget 16 --stress 6 --render-width 641 --render-height 385)
    add_test(NAME native_fixed_z_128 COMMAND avboit_viewer --headless --fixed-z --depth-budget 128 --stress 10 --sparse-blur)


    add_test(NAME native_inversion_board COMMAND avboit_viewer --headless --calibration-board --glass-sphere --no-smoke)

    add_test(NAME native_zero_t COMMAND avboit_viewer --headless --stress 11 --zero-t-shortcut)

    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/gltf-sample-assets/Models/Sponza/glTF/Sponza.gltf")
        add_test(NAME native_legacy_scene COMMAND avboit_viewer --headless --legacy-scene --sponza-path "${CMAKE_SOURCE_DIR}/assets/gltf-sample-assets/Models/Sponza/glTF/Sponza.gltf")
    endif()

    foreach(target avboit_viewer avboit_gpu_test avboit_options_test avboit_material_test avboit_order_test avboit_interface_test)
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    endforeach()

    add_test(NAME native_zero_depth_front COMMAND avboit_viewer --headless --stress 11 --hardware-depth)
    add_test(NAME native_zero_depth_back COMMAND avboit_viewer --headless --stress 12)
    add_test(NAME native_zero_depth_rgb COMMAND avboit_viewer --headless --stress 13)
    add_test(NAME native_zero_depth_smoke COMMAND avboit_viewer --headless --stress 14 --time 0.7)

    foreach(target avboit_gpu_test avboit_options_test avboit_material_test avboit_order_test avboit_interface_test avboit_camera_test)
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bin")
        foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/tests/bin")
        endforeach()
    endforeach()

    # Headless GPU tests produce diagnostic captures. Give each its own directory.
    get_property(avboit_tests DIRECTORY PROPERTY TESTS)
    foreach(test IN LISTS avboit_tests)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/captures/${test}")
        set_tests_properties(${test} PROPERTIES LABELS "gpu" RUN_SERIAL TRUE
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/captures/${test}")
    endforeach()
    set_tests_properties(viewer_options material_table submission_order interface_selection sample_camera viewer_help
        PROPERTIES LABELS "cpu")
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME asset_installer COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/test_asset_install.py)
        add_test(NAME repository_checks COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_repository.py)
        set_tests_properties(asset_installer repository_checks PROPERTIES LABELS "cpu")
    endif()
endif()
