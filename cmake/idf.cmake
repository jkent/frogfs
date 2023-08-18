include(${CMAKE_CURRENT_LIST_DIR}/files.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/functions.cmake)

idf_component_register(
SRCS
    ${libfrogfs_SRC}
INCLUDE_DIRS
    ${libfrogfs_INC}
PRIV_REQUIRES
    vfs
REQUIRES
    esp_partition
    spi_flash
)
