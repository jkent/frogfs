set(frogfs_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

if(ESP_PLATFORM)
    set(PROJECT_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/build/esp-idf)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if((NOT CMAKE_BUILD_EARLY_EXPANSION) AND (NOT CMAKE_NO_PIP_INSTALL))
    set(Python3_VENV ${PROJECT_BINARY_DIR}/CMakeFiles/venv/bin/python)

    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/CMakeFiles/venv.stamp ${PROJECT_BINARY_DIR}/CMakeFiles/venv
        COMMAND ${Python3_EXECUTABLE} -m venv ${PROJECT_BINARY_DIR}/CMakeFiles/venv
        COMMAND ${CMAKE_COMMAND} -E touch ${PROJECT_BINARY_DIR}/CMakeFiles/venv.stamp
        COMMENT "Initalizing Python virtualenv"
    )

    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/CMakeFiles/venv_requirements.stamp
        COMMAND ${PROJECT_BINARY_DIR}/CMakeFiles/venv/bin/pip install -r ${frogfs_DIR}/requirements.txt --upgrade
        COMMAND ${CMAKE_COMMAND} -E touch ${PROJECT_BINARY_DIR}/CMakeFiles/venv_requirements.stamp
        DEPENDS ${PROJECT_BINARY_DIR}/CMakeFiles/venv.stamp ${frogfs_DIR}/requirements.txt
        COMMENT "Installing Python requirements"
    )
elseif(CMAKE_NO_PIP_INSTALL)
    set(Python3_VENV python3)

    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/CMakeFiles/venv ${PROJECT_BINARY_DIR}/CMakeFiles/venv_requirements.stamp
        COMMAND ${CMAKE_COMMAND} -E touch ${PROJECT_BINARY_DIR}/CMakeFiles/venv
        COMMAND ${CMAKE_COMMAND} -E touch ${PROJECT_BINARY_DIR}/CMakeFiles/venv_requirements.stamp
        COMMENT "Fake fulfilling Python requirements"
    )
endif()

function(target_add_frogfs target path)
    cmake_parse_arguments(ARG "" "NAME;CONFIG" "" ${ARGN})
    if(NOT DEFINED ARG_NAME)
        get_filename_component(ARG_NAME ${path} NAME)
    endif()
    if(DEFINED ARG_CONFIG)
        set(ARG_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_CONFIG})
        set(config_yaml --config ${ARG_CONFIG})
    endif()
    set(output ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}.dir/${ARG_NAME})

    add_custom_target(frogfs_preprocess_${ARG_NAME}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${output}
        COMMAND ${Python3_VENV} ${frogfs_DIR}/tools/preprocess.py ${config_yaml} ${CMAKE_CURRENT_SOURCE_DIR}/${path} ${output}
        DEPENDS ${PROJECT_BINARY_DIR}/CMakeFiles/venv ${PROJECT_BINARY_DIR}/CMakeFiles/venv_requirements.stamp ${ARG_CONFIG}
        BYPRODUCTS ${PROJECT_BINARY_DIR}/CMakeFiles/node_modules ${output} ${output}/.state ${output}/.config
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/CMakeFiles
        COMMENT "Preprocessing ${ARG_NAME} files for ${target}"
        VERBATIM
        USES_TERMINAL
    )

    add_custom_command(OUTPUT ${output}.bin
        COMMAND ${Python3_VENV} ${frogfs_DIR}/tools/mkfrogfs.py ${output} ${output}.bin
        DEPENDS frogfs_preprocess_${ARG_NAME} ${output}/.state ${output}/.config
        COMMENT "Creating frogfs binary ${ARG_NAME}.bin"
        VERBATIM
        USES_TERMINAL
    )

    add_custom_command(OUTPUT ${output}_bin.c
        COMMAND ${Python3_VENV} ${frogfs_DIR}/tools/bin2c.py ${output}.bin ${output}_bin.c
        DEPENDS ${output}.bin
        COMMENT "Generating source file ${ARG_NAME}_bin.c"
        VERBATIM
    )
    target_sources(${target} PRIVATE ${output}_bin.c)
endfunction()

function(declare_frogfs_bin path)
    cmake_parse_arguments(ARG "" "NAME;CONFIG" "" ${ARGN})
    if(NOT DEFINED ARG_NAME)
        get_filename_component(ARG_NAME ${path} NAME)
    endif()
    if(DEFINED ARG_CONFIG)
        set(ARG_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_CONFIG})
        set(config_yaml --config ${ARG_CONFIG})
    endif()
    set(output ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${COMPONENT_LIB}.dir/frogfs_${ARG_NAME})

    add_custom_target(frogfs_preprocess_${ARG_NAME}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${output}
        COMMAND ${Python3_VENV} ${frogfs_DIR}/tools/preprocess.py ${config_yaml} ${CMAKE_CURRENT_SOURCE_DIR}/${path} ${output}
        DEPENDS ${PROJECT_BINARY_DIR}/CMakeFiles/venv ${PROJECT_BINARY_DIR}/CMakeFiles/venv_requirements.stamp ${ARG_CONFIG}
        BYPRODUCTS ${PROJECT_BINARY_DIR}/CMakeFiles/node_modules ${output} ${output}/.state ${output}/.config
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/CMakeFiles
        COMMENT "Preprocessing ${ARG_NAME} files"
        VERBATIM
        USES_TERMINAL
    )

    add_custom_command(OUTPUT ${output}.bin
        COMMAND ${Python3_VENV} ${frogfs_DIR}/tools/mkfrogfs.py ${output} ${output}.bin
        DEPENDS frogfs_preprocess_${ARG_NAME} ${output}/.state ${output}/.config
        COMMENT "Creating frogfs binary ${ARG_NAME}.bin"
        VERBATIM
        USES_TERMINAL
    )

    add_custom_target(generate_${ARG_NAME} DEPENDS ${output}.bin)
endfunction()
