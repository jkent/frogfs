/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once


/**
 * \brief       Maximum recursion depth for flat directory traversal. Uses
 *              sizeof(uint16_t) * n per open directory. Set to 1 to disable
 *              flat directory traversal.
 */
#if !defined(CONFIG_FROGFS_MAX_FLAT_DEPTH)
#define CONFIG_FROGFS_MAX_FLAT_DEPTH 8
#endif

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
