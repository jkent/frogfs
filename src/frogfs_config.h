#pragma once


/**
 * \brief       Maximum recursion depth for flat directory traversal. Uses
 *              sizeof(uint16_t) * n per open directory. Set to 1 to disable
 *              flat directory traversal.
 */
#define FROGFS_MAX_FLAT_DEPTH 8

#if !defined(CONFIG_FROGFS_USE_DEFLATE)
#define CONFIG_FROGFS_USE_DEFLATE 0
#endif

#if !defined(CONFIG_FROGFS_USE_HEATSHRINK)
#define CONFIG_FROGFS_USE_HEATSHRINK 0
#endif

#if !defined(CONFIG_FROGFS_LOG_LEVEL_NONE) || \
    !defined(CONFIG_FROGFS_LOG_LEVEL_ERROR) || \
    !defined(CONFIG_FROGFS_LOG_LEVEL_WARN) || \
    !defined(CONFIG_FROGFS_LOG_LEVEL_INFO) || \
    !defined(CONFIG_FROGFS_LOG_LEVEL_DEBUG) || \
    !defined(CONFIG_FROGFS_LOG_LEVEL_VERBOSE)
#define CONFIG_FROGFS_LEVEL_WARN 1
#endif
