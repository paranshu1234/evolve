cmake_minimum_required(VERSION 3.20)
project(Evolve VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  add_compile_options(/EHsc)
endif()
add_library(evolve_core src/core/Project.cpp src/core/Geometry.cpp
  src/core/CircuitTrace.cpp src/core/CircuitScene.cpp)
target_include_directories(evolve_core PUBLIC src)
if(MSVC)
  target_compile_options(evolve_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(evolve_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
include(CTest)
if(BUILD_TESTING)
  add_executable(evolve_tests tests/CoreTests.cpp)
  target_link_libraries(evolve_tests PRIVATE evolve_core)
  add_test(NAME evolve_core COMMAND evolve_tests)
  add_executable(evolve_circuit_trace_tests tests/CircuitTraceTests.cpp)
  target_link_libraries(evolve_circuit_trace_tests PRIVATE evolve_core)
  add_test(NAME evolve_circuit_trace COMMAND evolve_circuit_trace_tests)
  add_executable(evolve_circuit_scene_tests tests/CircuitSceneTests.cpp)
  target_link_libraries(evolve_circuit_scene_tests PRIVATE evolve_core)
  add_test(NAME evolve_circuit_scene COMMAND evolve_circuit_scene_tests)
endif()
add_executable(evolve_cli legacy/simulation.cpp)
add_executable(evolve_circuit_preview src/tools/CircuitPreview.cpp)
target_link_libraries(evolve_circuit_preview PRIVATE evolve_core)
if(BUILD_TESTING)
  add_test(NAME evolve_circuit_preview_cli COMMAND ${CMAKE_COMMAND}
    "-DPREVIEW=$<TARGET_FILE:evolve_circuit_preview>"
    "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/biology/examples/repressilator.csv"
    "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/circuit-preview-test"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/CircuitPreviewTests.cmake")
endif()
if(WIN32)
  add_executable(Evolve WIN32 src/desktop/Main.cpp src/desktop/CircuitWindow.cpp src/renderer/Dx12Renderer.cpp)
  target_compile_definitions(Evolve PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(Evolve PRIVATE evolve_core d3d12 dxgi d3dcompiler comctl32 comdlg32 shell32)
  if(MSVC)
    target_compile_options(Evolve PRIVATE /W4 /permissive-)
    set_property(TARGET Evolve evolve_core evolve_cli evolve_circuit_preview PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if(BUILD_TESTING)
      set_property(TARGET evolve_tests evolve_circuit_trace_tests evolve_circuit_scene_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
  endif()
  add_custom_command(TARGET Evolve POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Evolve>/circuit"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/biology/examples/repressilator.csv"
      "${CMAKE_CURRENT_SOURCE_DIR}/biology/examples/repressilator.json"
      "$<TARGET_FILE_DIR:Evolve>/circuit")
  install(TARGETS Evolve evolve_circuit_preview RUNTIME DESTINATION .)
  install(DIRECTORY biology/examples/ DESTINATION circuit)
  install(DIRECTORY biology DESTINATION .
    PATTERN ".venv" EXCLUDE PATTERN "__pycache__" EXCLUDE PATTERN "output" EXCLUDE)
  install(FILES README.md CHANGELOG.md DESTINATION .)
  install(DIRECTORY examples docs DESTINATION .)
endif()
