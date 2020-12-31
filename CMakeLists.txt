cmake_minimum_required(VERSION 3.5)

set(libespfs_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/espfs.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/espfs_vfs.c
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/heatshrink/heatshrink_decoder.c)
set(libespfs_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/third-party/heatshrink
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(libespfs_PRIV_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/src)
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
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/requirements.stamp
  COMMAND "${python}" -m pip install -r "${COMPONENT_DIR}/requirements.txt"
  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/requirements.stamp
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/espfsimage.bin
  COMMAND "${python}" "${COMPONENT_DIR}/tools/mkespfsimage.py" "${PROJECT_DIR}/${CONFIG_ESPFS_IMAGE_ROOT}" ${CMAKE_CURRENT_BINARY_DIR}/espfsimage.bin
  DEPENDS "${PROJECT_DIR}/${CONFIG_ESPFS_IMAGE_ROOT}" ${CMAKE_CURRENT_BINARY_DIR}/requirements.stamp
  COMMENT "Building espfsimage"
  USES_TERMINAL
  VERBATIM
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/espfsimage.c
  COMMAND "${python}" "${COMPONENT_DIR}/tools/bin2c.py" ${CMAKE_CURRENT_BINARY_DIR}/espfsimage.bin ${CMAKE_CURRENT_BINARY_DIR}/espfsimage.c
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/espfsimage.bin
  COMMENT "Generating espfs image C source"
  VERBATIM
)
