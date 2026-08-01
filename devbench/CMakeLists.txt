target_sources(${PROJECT_NAME} PRIVATE
    DevBenchAPI.cpp
    DevBenchBridge.cpp
    DevBenchInput.cpp
    DevBenchIntegration.cpp
    DevBenchSneakMatrix.cpp
)

target_compile_definitions(${PROJECT_NAME} PRIVATE SCS_WITH_DEVBENCH)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
