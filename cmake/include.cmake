get_filename_component(libespfs_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

set(libespfs_SRCS
    ${libespfs_DIR}/src/espfs.c
)

set(libespfs_INCLUDE_DIRS
    ${libespfs_DIR}/include
)

if(CONFIG_ESPFS_USE_HEATSHRINK)
    set(libespfs_SRCS ${libespfs_SRCS}
        ${libespfs_DIR}/third-party/heatshrink/heatshrink_decoder.c
    )
    set(libespfs_PRIV_INCLUDE_DIRS
        ${libespfs_DIR}/third-party/heatshrink
    )
endif()

if(CONFIG_IDF_TARGET_ESP8266 OR ESP_PLATFORM)
    set(libespfs_SRCS ${libespfs_SRCS}
        ${libespfs_DIR}/src/vfs.c
    )
    set(libespfs_PRIV_REQUIRES
        spi_flash
        vfs
    )
endif()

if(COMMAND idf_build_get_property)
    idf_build_get_property(python PYTHON)
else()
    set(python python)
endif()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/libespfs.dir/requirements.stamp
        DEPENDS ${libespfs_DIR}/requirements.txt
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/libespfs.dir
        COMMAND ${python} -m pip install -r ${libespfs_DIR}/requirements.txt
        COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/libespfs.dir/requirements.stamp
    )
endif()

function(define_target_espfs target dir output)
    if(IS_ABSOLUTE ${dir})
        file(RELATIVE_PATH dir ${PROJECT_SOURCE_DIR} ${dir})
    endif()

    get_filename_component(output_dir ${output} DIRECTORY)

    add_custom_target(${target}_paths
        BYPRODUCTS ${dir}/espfs.paths
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/libespfs.dir/requirements.stamp
        COMMAND ${python} ${libespfs_DIR}/tools/pathlist.py ${CMAKE_CURRENT_BINARY_DIR}/${dir}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Updating espfs.paths for ${target}"
        VERBATIM
    )

    add_custom_target(${target}
        BYPRODUCTS ${output}
        DEPENDS ${dir}/espfs.paths
        COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
        COMMAND ${python} ${libespfs_DIR}/tools/mkespfsimage.py ${dir} ${output}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Building espfs binary ${output}"
        USES_TERMINAL
        VERBATIM
    )
    add_dependencies(${target} ${target}_paths)
endfunction()

function(target_add_espfs target name)
    if(${ARGC} GREATER 1)
        set(dir ${ARGV2})
    else()
        set(dir ${name})
    endif()

    if(IS_ABSOLUTE ${dir})
        file(RELATIVE_PATH dir ${PROJECT_SOURCE_DIR} ${dir})
    endif()

    set(output ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}.dir/${name})
    file(RELATIVE_PATH rel_output ${CMAKE_CURRENT_BINARY_DIR} ${output})

    get_filename_component(output_dir ${output} DIRECTORY)

    add_custom_target(espfs_image_${name}
        BYPRODUCTS ${dir}/espfs.paths
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/libespfs.dir/requirements.stamp
        COMMAND ${python} ${libespfs_DIR}/tools/pathlist.py ${dir}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Updating pathlist for espfs_image_${name}"
        VERBATIM
    )
    add_dependencies(${target} espfs_image_${name})

    add_custom_command(OUTPUT ${output}.bin
        COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
        COMMAND ${python} ${libespfs_DIR}/tools/mkespfsimage.py ${dir} ${output}.bin
        DEPENDS ${dir}/espfs.paths
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Building espfs binary ${rel_output}"
        USES_TERMINAL
        VERBATIM
    )

    add_custom_command(OUTPUT ${output}.bin.c
        COMMAND ${python} ${libespfs_DIR}/tools/bin2c.py ${output}.bin ${output}.bin.c
        DEPENDS ${output}.bin
        COMMENT "Building source file ${rel_output}.c"
        VERBATIM
    )
    target_sources(${target} PRIVATE ${output}.bin.c)
endfunction()

function(target_config_vars)
    get_cmake_property(VARS VARIABLES)
    foreach(VAR ${VARS})
        if (VAR MATCHES "^CONFIG_")
            target_compile_definitions(${ARGV0} PUBLIC "-D${VAR}=${${VAR}}")
        endif()
    endforeach()
endfunction()
