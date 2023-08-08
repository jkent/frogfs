get_filename_component(frogfs_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE CACHE)

set(frogfs_SRC
    ${frogfs_DIR}/src/frogfs.c
    ${frogfs_DIR}/third-party/heatshrink/heatshrink_decoder.c
)

set(frogfs_INC
    ${frogfs_DIR}/include
    ${frogfs_DIR}/third-party/heatshrink
)

set(frogfs_IDF_SRC
    ${frogfs_DIR}/src/vfs.c
)

set(frogfs_IDF_PRIV_REQ
   esp_partition
   vfs
   spi_flash
)

set(frogfs_cwhttpd_SRC
    ${frogfs_DIR}/src/route.c
)
