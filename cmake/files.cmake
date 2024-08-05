get_filename_component(frogfs_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE CACHE)

set(libfrogfs_SRC
    ${frogfs_DIR}/src/frogfs.c
    ${frogfs_DIR}/src/decomp_raw.c
)

set(libfrogfs_INC
    ${frogfs_DIR}/include
)

if ("${CONFIG_FROGFS_USE_DEFLATE}" STREQUAL "y")
    if(ESP_PLATFORM)
        list(APPEND libfrogfs_SRC ${frogfs_DIR}/src/decomp_miniz_deflate.c)
    else()
        list(APPEND libfrogfs_SRC ${frogfs_DIR}/src/decomp_zlib_deflate.c)
    endif()
endif()

if ("${CONFIG_FROGFS_USE_HEATSHRINK}" STREQUAL "y")
    set(heatshrink_DIR ${frogfs_DIR}/third-party/heatshrink)
    list(APPEND libfrogfs_SRC ${frogfs_DIR}/src/decomp_heatshrink.c)
    list(APPEND libfrogfs_SRC ${heatshrink_DIR}/heatshrink_decoder.c)
    list(APPEND libfrogfs_INC ${heatshrink_DIR})
endif()

if(ESP_PLATFORM)
    list(APPEND libfrogfs_SRC
        ${frogfs_DIR}/src/vfs.c
    )
endif()
