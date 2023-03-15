include(CMakeFindDependencyMacro)

find_package(fmt REQUIRED)
find_package(freetype REQUIRED)
find_package(glad REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(minizip REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenGL REQUIRED)
find_package(PNG REQUIRED)
find_package(scn CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(Threads REQUIRED)
find_package(tinyxml2 REQUIRED)
find_package(Tracy REQUIRED)
find_package(ZLIB REQUIRED)

include("${CMAKE_CURRENT_LIST_DIR}/sgctTargets.cmake")


