set(frogfs_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

macro(generate_frogfs_rules path)
    get_filename_component(abspath ${path} ABSOLUTE)
    if(NOT IS_DIRECTORY "${abspath}")
        message(FATAL_ERROR "${path} is not a valid directory")
    endif()

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

    cmake_parse_arguments(ARG "" "NAME;CONFIG" "" ${ARGN})
    if(NOT DEFINED ARG_NAME)
        get_filename_component(ARG_NAME ${abspath} NAME)
    endif()
    if(DEFINED ARG_CONFIG)
        set(MKFROGFS_ARGS ${MKFROGFS_ARGS} --config ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_CONFIG})
    endif()
    set(output ${BUILD_DIR}/${ARG_NAME})
    set(build_output ${BUILD_DIR}/CMakeFiles/${ARG_NAME})

    add_custom_target(frogfs_preprocess_${ARG_NAME}
        COMMAND ${CMAKE_COMMAND} -E env CMAKEFILES_DIR=${BUILD_DIR}/CMakeFiles ${Python3_VENV_EXECUTABLE} ${frogfs_DIR}/tools/mkfrogfs.py ${directories} ${MKFROGFS_ARGS} ${path} ${output}.bin
        DEPENDS ${Python3_VENV}_requirements.stamp ${ARG_CONFIG}
        BYPRODUCTS ${build_output}/node_modules ${build_output}-cache ${build_output}-state.json ${output}.bin
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running mkfrogfs.py for ${ARG_NAME}.bin"
        USES_TERMINAL
    )
endmacro()

function(target_add_frogfs target path)
    LIST(REMOVE_AT ARGV 0)
    generate_frogfs_rules(${ARGV})

    add_custom_command(OUTPUT ${output}_bin.c
        COMMAND ${Python3_VENV_EXECUTABLE} ${frogfs_DIR}/tools/bin2c.py ${output}.bin ${output}_bin.c
        DEPENDS ${output}.bin
        COMMENT "Generating frogfs source file ${ARG_NAME}_bin.c"
    )
    target_sources(${target} PRIVATE ${output}_bin.c)
endfunction()

function(declare_frogfs_bin path)
    generate_frogfs_rules(${ARGV})

    add_custom_target(generate_${ARG_NAME}_bin DEPENDS frogfs_preprocess_${ARG_NAME} ${output}.bin)
endfunction()
