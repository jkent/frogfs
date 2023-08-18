include(${CMAKE_CURRENT_LIST_DIR}/files.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/functions.cmake)

add_library(frogfs
    ${libfrogfs_SRC}
)

target_include_directories(frogfs
PUBLIC
    ${libfrogfs_INC}
)

if("${CONFIG_FROGFS_USE_DEFLATE}" STREQUAL "y")
target_link_libraries(frogfs
    z
)
endif()

get_cmake_property(_variableNames VARIABLES)
list(SORT _variableNames)
foreach(_variableName ${_variableNames})
    unset(MATCHED)
    string(REGEX MATCH "^CONFIG_" MATCHED ${_variableName})
    if(NOT MATCHED)
        continue()
    endif()
    target_compile_definitions(frogfs
    PUBLIC
        "${_variableName}=${${_variableName}}"
    )
endforeach()
