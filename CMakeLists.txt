cmake_minimum_required (VERSION 2.6)
project (engine)

set(SOURCES engine.c)

set(CMAKE_BUILD_TYPE debug FORCE)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -0O")

find_package(OpenGL REQUIRED)
#find_package(SDL2 required)

include_directories(${OPENGL_INCLUDE_DIRE} )

add_executable(${PROJECT_NAME} ${SOURCES})

set(CMAKE_EXE_LINKER_FLAGS "-framework Cocoa -framework OpenGL")
set(BUILD_DIR ".")