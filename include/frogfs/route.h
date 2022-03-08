/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "libesphttpd/httpd.h"


/*************************
 * \section Espfs Routes
 *************************/

/**
 * \brief Espfs GET route handler
 */
ehttpd_status_t ehttpd_route_frogfs_get(ehttpd_conn_t *conn);

/**
 * \brief Espfs template route handler
 */
ehttpd_status_t ehttpd_route_frogfs_tpl(ehttpd_conn_t *conn);

/**
 * \brief Espfs index route handler
 */
ehttpd_status_t ehttpd_route_frogfs_index(ehttpd_conn_t *conn);

#ifdef __cplusplus
} /* extern "C" */
#endif
