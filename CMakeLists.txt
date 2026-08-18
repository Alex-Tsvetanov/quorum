cmake_minimum_required(VERSION 3.20)
project(quorum LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(quorum_core
  src/json.cpp
  src/model.cpp
  src/cpm.cpp
  src/mcdm.cpp
  src/portfolio.cpp
  src/risk.cpp
  src/sensitivity.cpp
  src/report.cpp
)
target_include_directories(quorum_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(quorum src/main.cpp src/bench.cpp)
target_link_libraries(quorum PRIVATE quorum_core)

enable_testing()
add_executable(quorum_tests
  tests/main.cpp
  tests/test_json.cpp
  tests/test_model.cpp
  tests/test_cpm.cpp
  tests/test_mcdm.cpp
  tests/test_portfolio.cpp
  tests/test_risk.cpp
  tests/test_sensitivity.cpp
)
target_link_libraries(quorum_tests PRIVATE quorum_core)
target_include_directories(quorum_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_definitions(quorum_tests PRIVATE
  QUORUM_SCENARIO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scenarios")

# One test binary, one CTest entry per suite. The runner filters by suite name.
foreach(suite json model cpm mcdm portfolio risk sensitivity)
  add_test(NAME ${suite} COMMAND quorum_tests ${suite})
endforeach()

# End-to-end: the demo must run and exit 0 on the bundled scenario.
add_test(NAME demo COMMAND quorum demo
         --scenario ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/example.json)

add_custom_target(demo
  COMMAND quorum demo --scenario ${CMAKE_CURRENT_SOURCE_DIR}/scenarios/example.json
  DEPENDS quorum
  USES_TERMINAL)
