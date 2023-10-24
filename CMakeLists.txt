cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/files.cmake)

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
