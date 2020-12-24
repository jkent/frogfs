cmake_minimum_required(VERSION 3.5)

set(libespfs_SRCS
  src/espfs.c
  src/espfs_vfs.c
  heatshrink/heatshrink_decoder.c)
set(libespfs_INCLUDE_DIRS
  heatshrink
  include)
set(libespfs_PRIV_INCLUDE_DIRS
  src)
set(libespfs_REQUIRES)
set(libespfs_PRIV_REQUIRES
  spi_flash)

if(CONFIG_ESPFS_EMBED_BINARY)
  set(libespfs_SRCS ${libespfs_SRCS}
    ${CMAKE_CURRENT_BINARY_DIR}/espfsimage.c)
endif()

idf_component_register(
  SRCS ${libespfs_SRCS}
  INCLUDE_DIRS ${libespfs_INCLUDE_DIRS}
  PRIV_INCLUDE_DIRS ${libespfs_PRIV_INCLUDE_DIRS}
  REQUIRES ${libespfs_REQUIRES}
  PRIV_REQUIRES ${libespfs_PRIV_REQUIRES}
)

idf_build_get_property(python PYTHON)

add_custom_command(
  OUTPUT requirements.stamp
  COMMAND "${python}" -m pip install -r "${COMPONENT_DIR}/requirements.txt"
  COMMAND ${CMAKE_COMMAND} -E touch requirements.stamp
)

add_custom_command(
  OUTPUT espfsimage.bin
  COMMAND "${python}" "${COMPONENT_DIR}/tools/mkespfsimage.py" "${CMAKE_SOURCE_DIR}/${CONFIG_ESPFS_IMAGE_ROOT}" espfsimage.bin
  DEPENDS "${CMAKE_SOURCE_DIR}/${CONFIG_ESPFS_IMAGE_ROOT}" requirements.stamp
  COMMENT "Building espfsimage"
  USES_TERMINAL
  VERBATIM
)

add_custom_command(
  OUTPUT espfsimage.c
  COMMAND "${python}" "${COMPONENT_DIR}/tools/bin2c.py" espfsimage.bin espfsimage.c
  DEPENDS espfsimage.bin
  COMMENT "Generating espfs image C source"
  VERBATIM
)
