set(frogfs_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

macro(generate_frogfs_rules)
    if(NOT ESP_PLATFORM)
        set(BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(Python3_VENV ${BUILD_DIR}/CMakeFiles/venv)
    if (CMAKE_HOST_WIN32)
        set(Python3_VENV_EXECUTABLE ${Python3_VENV}/Scripts/python.exe)
    else ()
        set(Python3_VENV_EXECUTABLE ${Python3_VENV}/bin/python)
    endif ()

    add_custom_command(OUTPUT ${Python3_VENV}.stamp ${Python3_VENV}
        COMMAND ${Python3_EXECUTABLE} -m venv ${Python3_VENV}
        COMMAND ${CMAKE_COMMAND} -E touch ${Python3_VENV}.stamp
        COMMENT "Initializing Python venv"
    )

    add_custom_command(OUTPUT ${Python3_VENV}_requirements.stamp
        COMMAND ${Python3_VENV_EXECUTABLE} -m pip install -r ${frogfs_DIR}/requirements.txt
        COMMAND ${CMAKE_COMMAND} -E touch ${Python3_VENV}_requirements.stamp
        DEPENDS ${Python3_VENV}.stamp ${frogfs_DIR}/requirements.txt
        COMMENT "Installing Python requirements"
    )

    cmake_parse_arguments(ARG "" "CONFIG;NAME" "TOOLS" ${ARGN})
    if(NOT DEFINED ARG_CONFIG)
        set(ARG_CONFIG frogfs.yaml)
    endif()
    if(NOT DEFINED ARG_NAME)
        set(ARG_NAME frogfs)
    endif()
    foreach(TOOL ARG_TOOLS)
        set(TOOLS ${TOOLS} --tools ${TOOL})
    endforeach()
    set(OUTPUT ${BUILD_DIR}/CMakeFiles/${ARG_NAME})

    add_custom_target(frogfs_preprocess_${ARG_NAME}
        COMMAND ${Python3_VENV_EXECUTABLE} ${frogfs_DIR}/tools/mkfrogfs.py -C ${CMAKE_SOURCE_DIR} ${TOOLS} ${ARG_CONFIG} ${BUILD_DIR} ${OUTPUT}.bin
        DEPENDS ${Python3_VENV}_requirements.stamp ${ARG_CONFIG}
        BYPRODUCTS ${BUILD_DIR}/node_modules ${BUILD_DIR}/${ARG_NAME}-cache ${BUILD_DIR}/${ARG_NAME}-cache-state.json ${OUTPUT}.bin
        COMMENT "Running mkfrogfs.py for ${ARG_NAME}.bin"
        USES_TERMINAL
    )
endmacro()

function(target_add_frogfs target)
    LIST(REMOVE_AT ARGV 0)
    generate_frogfs_rules(${ARGV})

    add_custom_command(OUTPUT ${OUTPUT}_bin.c
        COMMAND ${Python3_VENV_EXECUTABLE} ${frogfs_DIR}/tools/bin2c.py ${OUTPUT}.bin ${OUTPUT}_bin.c
        DEPENDS ${OUTPUT}.bin
        COMMENT "Generating frogfs source file ${ARG_NAME}_bin.c"
    )
    target_sources(${target} PRIVATE ${OUTPUT}_bin.c)
endfunction()

function(declare_frogfs_bin)
    generate_frogfs_rules(${ARGV})

    add_custom_target(generate_${ARG_NAME}_bin DEPENDS frogfs_preprocess_${ARG_NAME} ${OUTPUT}.bin)
endfunction()
