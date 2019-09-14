file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")

idf_component_register(
    SRCS "src/espfs_vfs.c"
         "src/espfs.c"
         "src/espfs_image.c"
         "heatshrink/src/heatshrink_decoder.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
                      "heatshrink/src"
                      "heatshrink/include"
                      "${CMAKE_CURRENT_BINARY_DIR}/include"
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
)

set(espfs_image_DEPENDS mkespfsimage)
if(npm_PACKAGES)
    add_custom_target(node_modules
        COMMAND npm install --save-dev ${npm_PACKAGES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    list(APPEND espfs_image_DEPENDS node_modules)
    foreach(name IN LISTS npm_BINARIES)
        add_custom_target(npm_link_${name}
            COMMAND mkdir -p bin/
            COMMAND ln -fs ../node_modules/.bin/${name} bin/
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            COMMENT "Updating ${name} node.js module..."
        )
        list(APPEND espfs_image_DEPENDS npm_link_${name})
    endforeach(name)
endif(npm_PACKAGES)


add_custom_target(espfs_image
    BYPRODUCTS "espfs_image_bin.h"
    COMMAND ${CMAKE_COMMAND} -E env
        "CMAKE_CURRENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
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
        "${COMPONENT_DIR}/build-image.py" "${PROJECT_DIR}/${CONFIG_ESPFS_IMAGEROOTDIR}"
    DEPENDS ${espfs_image_DEPENDS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generating espfs image..."
)

add_dependencies(${COMPONENT_LIB} espfs_image)