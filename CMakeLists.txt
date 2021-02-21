cmake_minimum_required(VERSION 3.5)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/include.cmake)

idf_component_register(
    SRCS ${libespfs_SRCS}
    INCLUDE_DIRS ${libespfs_INCLUDE_DIRS}
    PRIV_INCLUDE_DIRS ${libespfs_PRIV_INCLUDE_DIRS}
    PRIV_REQUIRES ${libespfs_PRIV_REQUIRES}
)
