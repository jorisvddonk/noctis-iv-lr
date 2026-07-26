cmake_minimum_required(VERSION 3.14)

project(nivlr)

# CPM-provided packages (built from source)
include(cmake/CPM.cmake)
CPMAddPackage(
    NAME raylib
    GITHUB_REPOSITORY raysan5/raylib
    GIT_TAG 6.0
)
CPMAddPackage(
    NAME glm
    GITHUB_REPOSITORY g-truc/glm
    GIT_TAG 1.0.3
)

if (UNIX)
    set(CMAKE_C_COMPILER "/usr/bin/clang")
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
endif()

if (EMSCRIPTEN)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s USE_GLFW=3 -s ASSERTIONS=1 -s WASM=1 --emrun --preload-file res/supports.nct --preload-file data/GUIDE.BIN --preload-file data/STARMAP.BIN")
    set(CMAKE_EXECUTABLE_SUFFIX ".html") # This line is used to set your executable to build with the emscripten html template so that you can directly open it.
endif()

set(SOURCE_FILES src/noctis.cpp src/noctis-1.cpp src/noctis-0.cpp src/brtl.cpp)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

if (EMSCRIPTEN)
    # PLEASE NOTE: If you are trying to build a WASM version of this yourself, you will
    # have to manually build a WASM version of Raylib and touch up the paths below.
    target_include_directories(${PROJECT_NAME} PRIVATE /home/dcole/Projects/raylib/src/)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/include/glm/)
    target_link_libraries(${PROJECT_NAME} /home/dcole/Projects/raylib/src/libraylib.a)
else()
    target_link_libraries(${PROJECT_NAME} raylib)
    target_link_libraries(${PROJECT_NAME} glm::glm)
endif()

add_custom_command(TARGET nivlr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_SOURCE_DIR}/res
        ${CMAKE_CURRENT_BINARY_DIR}/res
    COMMENT "Symlinking resources"
)