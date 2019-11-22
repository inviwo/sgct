# Copyright Linkoping University 2011
# SGCT Project Authors see Authors.txt

add_executable(openvr main.cpp gridShader.frag gridShader.vert pyramidShader.frag pyramidShader.vert)
set_compile_options(openvr)
target_link_libraries(openvr PRIVATE sgct)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/support/cmake/modules/")

# OpenVR Specific
find_package(OpenVR REQUIRED)
target_include_directories(openvr PRIVATE
  ${OPENVR_INCLUDE_DIRS}
)
target_link_libraries(openvr PRIVATE
  ${OPENVR_LIBRARY}
)

set(files
  "${CMAKE_CURRENT_SOURCE_DIR}/openvr_window_same_as_hmd.xml"
  "${CMAKE_CURRENT_SOURCE_DIR}/openvr_with_fullscreen_mono.xml"
  "${CMAKE_CURRENT_SOURCE_DIR}/run.bat"
  "${CMAKE_CURRENT_SOURCE_DIR}/run.command"
  "${CMAKE_CURRENT_SOURCE_DIR}/run_fullscreen.bat"
  "${CMAKE_CURRENT_SOURCE_DIR}/run_fullscreen.command"
)
copy_files(openvr ${files})
copy_sgct_dynamic_libraries(openvr)
set_target_properties(openvr PROPERTIES FOLDER "Examples")