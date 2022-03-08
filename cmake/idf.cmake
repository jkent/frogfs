get_filename_component(frogfs_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE CACHE)

idf_component_register(
SRCS
    ${frogfs_SRC}
    ${frogfs_IDF_SRC}
INCLUDE_DIRS
    ${frogfs_INC}
PRIV_REQUIRES
    ${frogfs_IDF_PRIV_REQ}
)
