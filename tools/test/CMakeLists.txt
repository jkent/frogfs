cmake_minimum_required(VERSION 3.5)

project(test)

set(CONFIG_FROGFS_USE_HEATSHRINK y)
set(CONFIG_FROGFS_LOG_LEVEL_VERBOSE y)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/include.cmake)

add_executable(test
    ${frogfs_SRCS}
    "main.c"
)

target_include_directories(test
PRIVATE
    ${frogfs_INCLUDE_DIRS}
    ${frogfs_PRIV_INCLUDE_DIRS}
)
target_config_vars(test)
