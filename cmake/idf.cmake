include(${CMAKE_CURRENT_LIST_DIR}/files.cmake)

idf_component_register(
SRCS
    ${frogfs_SRC}
    ${frogfs_IDF_SRC}
INCLUDE_DIRS
    ${frogfs_INC}
PRIV_INCLUDE_DIRS
    ${frogfs_PRIV_INC}
PRIV_REQUIRES
    ${frogfs_IDF_PRIV_REQ}
)
