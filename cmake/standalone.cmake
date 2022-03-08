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

if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/../../libespfs)
    include(${CMAKE_CURRENT_LIST_DIR}/../../libespfs/cmake/files.cmake)

    target_sources(frogfs
    PUBLIC
        ${ehttpd_SRC}
    )
    target_include_directories(frogfs
    PUBLIC
        ${ehttpd_INC}
    )
endif()
