cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_OBJECTS 1)
set(CMAKE_CXX_RESPONSE_FILE_LINK_FLAG "@")

if (NOT PLATFORM_VITA)
    message(FATAL_ERROR "nestopia-vita only supports PLATFORM_VITA=ON")
endif ()

set(OPTION_BUILTIN_LIBCONFIG OFF CACHE BOOL "Build libconfig from sources")
set(OPTION_UPDATE_CONFIG OFF CACHE BOOL "Force config file updating")

# libcross2d handles Vita toolchain setup.
add_subdirectory(external/libcross2d)

project(nestopia-vita)
set(PROJECT_AUTHOR cpasjuste)
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)

if (PLATFORM_VITA AND NOT TARGET_PLATFORM)
    set(TARGET_PLATFORM vita CACHE STRING "" FORCE)
endif ()

add_subdirectory(src/runtime)

##############
# NESTOPIA
##############
set(NESTOPIA_DIR ${CMAKE_SOURCE_DIR}/external/nestopia)
set(NESTOPIA_INC
        ${NESTOPIA_DIR}/source
        ${NESTOPIA_DIR}/source/core
        ${NESTOPIA_DIR}/source/core/api
        ${NESTOPIA_DIR}/source/core/board
        ${NESTOPIA_DIR}/source/core/input
        ${NESTOPIA_DIR}/source/core/vssystem
        ${NESTOPIA_DIR}/source/fltkui
)

file(GLOB NESTOPIA_SRC
        ${NESTOPIA_DIR}/source/core/*.c*
        ${NESTOPIA_DIR}/source/core/api/*.c*
        ${NESTOPIA_DIR}/source/core/board/*.c*
        ${NESTOPIA_DIR}/source/core/input/*.c*
        ${NESTOPIA_DIR}/source/core/vssystem/*.c*
        ${NESTOPIA_DIR}/source/fltkui/jg.cpp
)
# Remove expensive video filters not needed on Vita
list(REMOVE_ITEM NESTOPIA_SRC
        ${NESTOPIA_DIR}/source/core/NstVideoFilter2xSaI.cpp
        ${NESTOPIA_DIR}/source/core/NstVideoFilterHqX.cpp
        ${NESTOPIA_DIR}/source/core/NstVideoFilterScaleX.cpp
        ${NESTOPIA_DIR}/source/core/NstVideoFilterxBR.cpp
        ${NESTOPIA_DIR}/source/core/NstVideoFilterNtsc.cpp
)

set(NESTOPIA_CFLAGS
        -DVERSION="1.51.1+"
        -D_MINGW
        -DNST_NO_2XSAI -DNST_NO_HQ2X -DNST_NO_SCALEX -DNO_NTSC -DNST_NO_XBR
        -D__PNES__ -D__NESTOPIA_VITA__
        -D__PEMU_VERSION_MAJOR__=${VERSION_MAJOR}
        -D__PEMU_VERSION_MINOR__=${VERSION_MINOR}
        -Dstrcasecmp=strcmp -DNST_DATADIR="app0:"
        -Wno-unused-local-typedefs
        -Wno-sign-compare -Wno-unused-variable
        -Wno-delete-non-virtual-dtor -Wno-reorder
        -Wno-parentheses -Wno-unknown-pragmas
        -Wno-format-truncation -Wno-unused-but-set-variable
        -Wno-format-zero-length
)

##############
# nestopia-vita sources
##############
file(GLOB SRC_VITA
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/src/*.c
)
list(APPEND SRC_VITA ${CMAKE_SOURCE_DIR}/src/vita/port.c)

##############
# deps
##############
include(FindPkgConfig)
pkg_search_module(LIBARCHIVE REQUIRED libarchive)

set(CMAKE_CXX_STANDARD 17)

set(TITLE_ID "NEST00001" CACHE STRING "" FORCE)

set(SRC ${SRC_VITA} ${NESTOPIA_SRC})

add_executable(${PROJECT_NAME} ${SRC})
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${NESTOPIA_INC}
)
target_compile_options(${PROJECT_NAME} PRIVATE ${NESTOPIA_CFLAGS})
target_link_libraries(${PROJECT_NAME} PRIVATE
        cross2dui cross2d
        ${LIBARCHIVE_LIBRARIES}
        z bz2 zstd
        ScePower_stub pthread
)

include(${CMAKE_SOURCE_DIR}/external/libcross2d/cmake/targets.cmake)

# add common skin data
add_custom_target(${PROJECT_NAME}.data.skin
        COMMAND ${CMAKE_COMMAND} -D SRC=${CMAKE_SOURCE_DIR}/data/common/romfs -D DST=${CMAKE_CURRENT_BINARY_DIR}/data_romfs -P ${CMAKE_SOURCE_DIR}/external/libcross2d/cmake/copy_custom.cmake
        COMMAND ${CMAKE_COMMAND} -D SRC=${CMAKE_SOURCE_DIR}/data/${TARGET_PLATFORM}/romfs -D DST=${CMAKE_CURRENT_BINARY_DIR}/data_romfs -P ${CMAKE_SOURCE_DIR}/external/libcross2d/cmake/copy_custom.cmake
)
add_dependencies(${PROJECT_NAME}.data.skin ${PROJECT_NAME}.data)
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data.skin)

# add extra data: nestopia database
add_custom_target(${PROJECT_NAME}.data.extra
        DEPENDS ${PROJECT_NAME}.data
        COMMAND ${CMAKE_COMMAND} -E copy ${NESTOPIA_DIR}/NstDatabase.xml ${CMAKE_CURRENT_BINARY_DIR}/data_romfs/
)
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data.extra)
