cmake_minimum_required (VERSION 2.6)
project (mkespfsimage)

set(mkespfsimage_SOURCES "${PROJECT_SOURCE_DIR}/main.c")
include_directories ("${PROJECT_SOURCE_DIR}/../include")

OPTION(USE_GZIP_COMPRESSION
    "Use gzip compression" OFF)
if (USE_GZIP_COMPRESSION)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DESPFS_GZIP")
    find_package(ZLIB)
endif (USE_GZIP_COMPRESSION)

OPTION(USE_HEATSHRINK
    "Use heatshrink compression" ON)
if (USE_HEATSHRINK)
    include_directories (
        "${PROJECT_SOURCE_DIR}/../heatshrink/src"
        "${PROJECT_SOURCE_DIR}/../heatshrink/include"
    )
    list(APPEND mkespfsimage_SOURCES "${PROJECT_SOURCE_DIR}/../heatshrink/src/heatshrink_encoder.c")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DESPFS_HEATSHRINK")
endif (USE_HEATSHRINK)

add_executable (mkespfsimage "${mkespfsimage_SOURCES}")
if (USE_GZIP_COMPRESSION)
    target_link_libraries(mkespfsimage ${ZLIB_LIBRARIES})
endif (USE_GZIP_COMPRESSION)

install(TARGETS mkespfsimage RUNTIME DESTINATION "mkespfsimage")
