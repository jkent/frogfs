if(CONFIG_ESPFS_LINK_BINARY)
    list(APPEND espfs_SRCS "${CMAKE_CURRENT_BINARY_DIR}/src/espfs_image.c")
endif(CONFIG_ESPFS_LINK_BINARY)

idf_component_register(
    SRCS "src/espfs_vfs.c"
         "src/espfs.c"
         "heatshrink/heatshrink_decoder.c"
         "${espfs_SRCS}"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
                      "heatshrink"
    REQUIRES "spi_flash"
)

idf_build_get_property(python PYTHON)

add_custom_command(
    OUTPUT requirements.stamp
    COMMAND "${python}" -m pip install -r "${COMPONENT_DIR}/requirements.txt"
    COMMAND ${CMAKE_COMMAND} -E touch requirements.stamp
)

set(espfs_image_bin_DEPS "")
if (EXISTS "${PROJECT_DIR}/espfs.yaml")
    list(APPEND "${PROJECT_DIR}/espfs.yaml")
endif()

add_custom_command(
    OUTPUT espfs_image.bin
    COMMAND ${CMAKE_COMMAND} -E env PROJECT_DIR=${PROJECT_DIR} "${python}" "${COMPONENT_DIR}/tools/mkespfsimage.py" "${PROJECT_DIR}/${CONFIG_ESPFS_IMAGEROOTDIR}" espfs_image.bin
    DEPENDS "${PROJECT_DIR}/${CONFIG_ESPFS_IMAGEROOTDIR}" requirements.stamp ${espfs_image_bin_DEPS}
    COMMENT "Building espfs image"
    USES_TERMINAL
    VERBATIM
)

add_custom_command(
    OUTPUT src/espfs_image.c
    COMMAND "${python}" "${COMPONENT_DIR}/tools/bin2c.py" espfs_image.bin src/espfs_image.c
    DEPENDS espfs_image.bin
    COMMENT "Generating espfs image C source"
    VERBATIM
)