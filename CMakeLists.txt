file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

if(CONFIG_ESPFS_LINK_BINARY)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src")
    list(APPEND espfs_SRCS "${CMAKE_CURRENT_BINARY_DIR}/src/espfs_image.c")
endif(CONFIG_ESPFS_LINK_BINARY)

idf_component_register(
    SRCS "src/espfs_vfs.c"
         "src/espfs.c"
         "heatshrink/src/heatshrink_decoder.c"
         "${espfs_SRCS}"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
                      "heatshrink/src"
                      "heatshrink/include"
    REQUIRES "spi_flash"
)

if(CONFIG_ESPFS_USE_BABEL)
    list(APPEND npm_PACKAGES @babel/core @babel/cli @babel/preset-env babel-preset-minify)
    list(APPEND npm_BINARIES babel)
endif(CONFIG_ESPFS_USE_BABEL)

if(CONFIG_ESPFS_USE_HTMLMINIFIER)
    list(APPEND npm_PACKAGES html-minifier)
    list(APPEND npm_BINARIES html-minifier)
endif(CONFIG_ESPFS_USE_HTMLMINIFIER)

if(CONFIG_ESPFS_USE_UGLIFYCSS)
    list(APPEND npm_PACKAGES uglifycss)
    list(APPEND npm_BINARIES uglifycss)
endif(CONFIG_ESPFS_USE_UGLIFYCSS)

if(CONFIG_ESPFS_USE_UGLIFYJS)
    list(APPEND npm_PACKAGES uglify-js)
    list(APPEND npm_BINARIES uglifyjs)
endif(CONFIG_ESPFS_USE_UGLIFYJS)

externalproject_add(mkespfsimage
    SOURCE_DIR "${COMPONENT_DIR}/mkespfsimage"
    CMAKE_ARGS
        "-DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}"
        "-DUSE_HEATSHRINK=${CONFIG_ESPFS_USE_HEATSHRINK}"
        "-DUSE_GZIP_COMPRESSION=${CONFIG_ESPFS_USE_GZIP}"
)

set(espfs_image_DEPENDS mkespfsimage)
if(npm_PACKAGES)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/node_modules"
        COMMAND npm install --save-dev ${npm_PACKAGES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    list(APPEND espfs_image_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/node_modules")

    foreach(name IN LISTS npm_BINARIES)
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/bin/${name}"
            COMMAND ln -fs "../node_modules/.bin/${name}" bin/
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/node_modules"
        )
        list(APPEND espfs_image_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/bin/${name}")
    endforeach(name)
endif(npm_PACKAGES)

if (NOT CONFIG_ESPFS_CSS_MINIFY_UGLIFYCSS)
    set(CONFIG_ESPFS_CSS_MINIFY_UGLIFYCSS "n")
    set(CONFIG_ESPFS_UGLIFYCSS_PATH "")
endif(NOT CONFIG_ESPFS_CSS_MINIFY_UGLIFYCSS)

if (NOT CONFIG_ESPFS_HTML_MINIFY_HTMLMINIFIER)
    set(CONFIG_ESPFS_HTML_MINIFY_HTMLMINIFIER "n")
    set(CONFIG_ESPFS_HTMLMINIFIER_PATH "")
endif(NOT CONFIG_ESPFS_HTML_MINIFY_HTMLMINIFIER)

if (NOT CONFIG_ESPFS_JS_CONVERT_BABEL)
    set(CONFIG_ESPFS_JS_CONVERT_BABEL "n")
endif(NOT CONFIG_ESPFS_JS_CONVERT_BABEL)

if (NOT CONFIG_ESPFS_JS_MINIFY_BABEL)
    set(CONFIG_ESPFS_JS_MINIFY_BABEL "n")
endif(NOT CONFIG_ESPFS_JS_MINIFY_BABEL)

if (NOT (CONFIG_ESPFS_JS_CONVERT_BABEL OR CONFIG_ESPFS_JS_MINIFY_BABEL))
    set(CONFIG_ESPFS_BABEL_PATH "")
endif(NOT (CONFIG_ESPFS_JS_CONVERT_BABEL OR CONFIG_ESPFS_JS_MINIFY_BABEL))

if (NOT CONFIG_ESPFS_JS_MINIFY_UGLIFYJS)
    set(CONFIG_ESPFS_JS_MINIFY_UGLIFYJS "n")
    set(CONFIG_ESPFS_UGLIFYJS_PATH "")
endif(NOT CONFIG_ESPFS_JS_MINIFY_UGLIFYJS)

idf_build_get_property(python PYTHON)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/espfs_image.bin"
    COMMAND ${CMAKE_COMMAND} -E env
        "BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        "CONFIG_ESPFS_PREPROCESS_FILES=${CONFIG_ESPFS_PREPROCESS_FILES}"
        "CONFIG_ESPFS_CSS_MINIFY_UGLIFYCSS=${CONFIG_ESPFS_CSS_MINIFY_UGLIFYCSS}"
        "CONFIG_ESPFS_HTML_MINIFY_HTMLMINIFIER=${CONFIG_ESPFS_HTML_MINIFY_HTMLMINIFIER}"
        "CONFIG_ESPFS_JS_CONVERT_BABEL=${CONFIG_ESPFS_JS_CONVERT_BABEL}"
        "CONFIG_ESPFS_JS_MINIFY_BABEL=${CONFIG_ESPFS_JS_MINIFY_BABEL}"
        "CONFIG_ESPFS_JS_MINIFY_UGLIFYJS=${CONFIG_ESPFS_JS_MINIFY_UGLIFYJS}"
        "CONFIG_ESPFS_UGLIFYCSS_PATH=${CONFIG_ESPFS_UGLIFYCSS_PATH}"
        "CONFIG_ESPFS_HTMLMINIFIER_PATH=${CONFIG_ESPFS_HTMLMINIFIER_PATH}"
        "CONFIG_ESPFS_BABEL_PATH=${CONFIG_ESPFS_BABEL_PATH}"
        "CONFIG_ESPFS_UGLIFYJS_PATH=${CONFIG_ESPFS_UGLIFYJS_PATH}"
        "${python}" "${COMPONENT_DIR}/tools/build-image.py" "${PROJECT_DIR}/${CONFIG_ESPFS_IMAGEROOTDIR}"
    DEPENDS ${espfs_image_DEPENDS} "${PROJECT_DIR}/${CONFIG_ESPFS_IMAGEROOTDIR}"
    VERBATIM
)
add_custom_target(espfs_image DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/espfs_image.bin")
add_dependencies(${COMPONENT_LIB} espfs_image)

add_custom_command(OUTPUT src/espfs_image.c
    COMMAND xxd -i espfs_image.bin src/espfs_image.c
    COMMAND sed -i "1s;^;const __attribute__((aligned(4))) ;" src/espfs_image.c

    DEPENDS espfs_image.bin
    VERBATIM
)
add_custom_target(espfs_image_c DEPENDS src/espfs_image.c)
add_dependencies(${COMPONENT_LIB} espfs_image_c)
