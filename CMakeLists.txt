cmake_minimum_required(VERSION 3.16)

if(ESP_PLATFORM)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/idf.cmake)
else()
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/standalone.cmake)
endif()
