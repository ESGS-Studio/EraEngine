include(${CMAKE_SOURCE_DIR}/cmake/common.cmake)

era_begin(example_game "APP")
    require_thirdparty_module(example_game EnTT)
    require_thirdparty_module(example_game yaml-cpp)
    require_thirdparty_module(example_game rttr_core)
    require_thirdparty_module(example_game DirectXTex)
    require_module(example_game base)
    require_module(example_game core)
    require_module(example_game physics)
    require_module(example_game simple_motion_matching)

    require_physx(example_game)

    target_include_directories(example_game PUBLIC ${ERA_ENGINE_PATH}/modules/thirdparty-imgui/imgui)
era_end(example_game)