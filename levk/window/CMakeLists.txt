project(levk-window)

# Library
add_library(${PROJECT_NAME})
add_library(levk::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_compile_definitions(${PROJECT_NAME} PUBLIC $<$<BOOL:${LEVK_USE_GLFW}>:LEVK_USE_GLFW LEVK_DESKTOP>)
target_include_directories(${PROJECT_NAME}
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    src
)
target_link_libraries(${PROJECT_NAME}
  PUBLIC
    dyvk::dyvk
    levk::levk-core
    $<$<BOOL:${LEVK_USE_GLFW}>:glfw>
  PRIVATE
    levk::levk-compile-options
)

target_sources(${PROJECT_NAME} PRIVATE
  include/levk/window/event.hpp
  include/levk/window/glue.hpp
  include/levk/window/window.hpp
  include/levk/window/types.hpp
)
add_subdirectory(src)
target_source_group(TARGET ${PROJECT_NAME})

# PCH
if(LEVK_USE_PCH)
  target_precompile_headers(${PROJECT_NAME} REUSE_FROM levk-core)
endif()

if(LEVK_INSTALL)
  install(TARGETS levk-window EXPORT levk-window-targets)
  install(EXPORT levk-window-targets
    FILE levk-window-targets.cmake
    NAMESPACE levk::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/levk
  )
  install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
  )
endif()
