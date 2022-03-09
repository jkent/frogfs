include(${CMAKE_CURRENT_LIST_DIR}/files.cmake)

add_library(frogfs
    ${frogfs_SRC}
)

target_include_directories(frogfs
PUBLIC
    ${frogfs_INC}
PRIVATE
    ${frogfs_PRIV_INC}
)
