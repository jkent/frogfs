/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#if defined(CONFIG_FROGFS_LOG_LEVEL_NONE)
# define LOG_LEVEL LOG_NONE
#elif defined(CONFIG_FROGFS_LOG_LEVEL_ERROR)
# define LOG_LEVEL LOG_ERROR
#elif defined(CONFIG_FROGFS_LOG_LEVEL_WARN)
# define LOG_LEVEL LOG_WARN
#elif defined(CONFIG_FROGFS_LOG_LEVEL_INFO)
# define LOG_LEVEL LOG_INFO
#elif defined(CONFIG_FROGFS_LOG_LEVEL_DEBUG)
# define LOG_LEVEL LOG_DEBUG
#elif defined(CONFIG_FROGFS_LOG_LEVEL_VERBOSE)
# define LOG_LEVEL LOG_VERBOSE
#else
# define LOG_LEVEL LOG_WARN
#endif

#if defined(ESP_PLATFORM)

# include <esp_log.h>


# define LOG_NONE    ESP_LOG_NONE
# define LOG_ERROR   ESP_LOG_ERROR
# define LOG_WARN    ESP_LOG_WARN
# define LOG_INFO    ESP_LOG_INFO
# define LOG_DEBUG   ESP_LOG_DEBUG
# define LOG_VERBOSE ESP_LOG_VERBOSE

# if defined(CONFIG_IDF_TARGET_ESP8266)
#  define LOGE( format, ... )  if (LOG_LEVEL >= LOG_ERROR)   { esp_log_write(LOG_ERROR,   __func__, format, ##__VA_ARGS__); }
#  define LOGW( format, ... )  if (LOG_LEVEL >= LOG_WARN)    { esp_log_write(LOG_WARN,    __func__, format, ##__VA_ARGS__); }
#  define LOGI( format, ... )  if (LOG_LEVEL >= LOG_INFO)    { esp_log_write(LOG_INFO,    __func__, format, ##__VA_ARGS__); }
#  define LOGD( format, ... )  if (LOG_LEVEL >= LOG_DEBUG)   { esp_log_write(LOG_DEBUG,   __func__, format, ##__VA_ARGS__); }
#  define LOGV( format, ... )  if (LOG_LEVEL >= LOG_VERBOSE) { esp_log_write(LOG_VERBOSE, __func__, format, ##__VA_ARGS__); }
# else
#  define LOGE( format, ... )  if (LOG_LEVEL >= LOG_ERROR)   { esp_log_write(LOG_ERROR,   __func__, LOG_FORMAT(E, format), esp_log_timestamp(), __func__, ##__VA_ARGS__); }
#  define LOGW( format, ... )  if (LOG_LEVEL >= LOG_WARN)    { esp_log_write(LOG_WARN,    __func__, LOG_FORMAT(W, format), esp_log_timestamp(), __func__, ##__VA_ARGS__); }
#  define LOGI( format, ... )  if (LOG_LEVEL >= LOG_INFO)    { esp_log_write(LOG_INFO,    __func__, LOG_FORMAT(I, format), esp_log_timestamp(), __func__, ##__VA_ARGS__); }
#  define LOGD( format, ... )  if (LOG_LEVEL >= LOG_DEBUG)   { esp_log_write(LOG_DEBUG,   __func__, LOG_FORMAT(D, format), esp_log_timestamp(), __func__, ##__VA_ARGS__); }
#  define LOGV( format, ... )  if (LOG_LEVEL >= LOG_VERBOSE) { esp_log_write(LOG_VERBOSE, __func__, LOG_FORMAT(V, format), esp_log_timestamp(), __func__, ##__VA_ARGS__); }
# endif

#else

# include <stdio.h>


typedef enum {
    LOG_NONE,
    LOG_ERROR,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG,
    LOG_VERBOSE
} log_level_t;

# define LOG_COLOR_RED     "31"
# define LOG_COLOR_GREEN   "32"
# define LOG_COLOR_BROWN   "33"
# define LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
# define LOG_RESET_COLOR   "\033[0m"

# define LOG_COLOR_E       LOG_COLOR(LOG_COLOR_RED)
# define LOG_COLOR_W       LOG_COLOR(LOG_COLOR_BROWN)
# define LOG_COLOR_I       LOG_COLOR(LOG_COLOR_GREEN)
# define LOG_COLOR_D
# define LOG_COLOR_V

# define LOG_FORMAT(letter, format)  LOG_COLOR_ ## letter #letter " %s: " format LOG_RESET_COLOR "\n"

# define LOGE( format, ... )  if (LOG_LEVEL >= LOG_ERROR)   { fprintf(stderr, LOG_FORMAT(E, format), __func__, ##__VA_ARGS__); }
# define LOGW( format, ... )  if (LOG_LEVEL >= LOG_WARN)    { fprintf(stderr, LOG_FORMAT(W, format), __func__, ##__VA_ARGS__); }
# define LOGI( format, ... )  if (LOG_LEVEL >= LOG_INFO)    { fprintf(stderr, LOG_FORMAT(I, format), __func__, ##__VA_ARGS__); }
# define LOGD( format, ... )  if (LOG_LEVEL >= LOG_DEBUG)   { fprintf(stderr, LOG_FORMAT(D, format), __func__, ##__VA_ARGS__); }
# define LOGV( format, ... )  if (LOG_LEVEL >= LOG_VERBOSE) { fprintf(stderr, LOG_FORMAT(V, format), __func__, ##__VA_ARGS__); }

#endif
