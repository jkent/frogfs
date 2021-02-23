cmake_minimum_required(VERSION 3.5)

project(test)

set(CONFIG_ESPFS_USE_HEATSHRINK y)
set(CONFIG_ESPFS_LOG_LEVEL_VERBOSE y)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/include.cmake)

add_executable(test
    ${libespfs_SRCS}
    "main.c"
)

target_include_directories(test
PRIVATE
    ${libespfs_INCLUDE_DIRS}
    ${libespfs_PRIV_INCLUDE_DIRS}
)
target_config_vars(test)
