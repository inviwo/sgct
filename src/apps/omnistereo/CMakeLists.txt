##########################################################################################
# SGCT                                                                                   #
# Simple Graphics Cluster Toolkit                                                        #
#                                                                                        #
# Copyright (c) 2012-2020                                                                #
# For conditions of distribution and use, see copyright notice in LICENSE.md             #
##########################################################################################

add_executable(omnistereo main.cpp)
set_compile_options(omnistereo)
find_package(glm REQUIRED)
target_link_libraries(omnistereo PRIVATE sgct glm)

set(files
  "${CMAKE_CURRENT_SOURCE_DIR}/sepmap.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/test.xml"
  "${CMAKE_CURRENT_SOURCE_DIR}/turnmap.jpg"
  "${CMAKE_CURRENT_SOURCE_DIR}/../SharedResources/box.png"
)
copy_files(omnistereo ${files})
copy_sgct_dynamic_libraries(omnistereo)
set_property(TARGET omnistereo PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:omnistereo>)
set_target_properties(omnistereo PROPERTIES FOLDER "Examples")
