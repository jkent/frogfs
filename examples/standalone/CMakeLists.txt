cmake_minimum_required(VERSION 3.13)

project(example)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CONFIG_FROGFS_USE_DEFLATE y)
set(CONFIG_FROGFS_USE_HEATSHRINK y)
set(CONFIG_FROGFS_LOG_LEVEL_DEBUG y)

include(../../CMakeLists.txt)

add_executable(example
    main.c
)
target_link_libraries(example
    frogfs
)
target_add_frogfs(example ../files NAME frogfs CONFIG frogfs.yaml)
