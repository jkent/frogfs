/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "cwhttpd/httpd.h"


/*************************
 * \section Espfs Routes
 *************************/

/**
 * \brief Espfs GET route handler
 */
cwhttpd_status_t frogfs_route_get(cwhttpd_conn_t *conn);

/**
 * \brief Espfs template route handler
 */
cwhttpd_status_t frogfs_route_tpl(cwhttpd_conn_t *conn);

/**
 * \brief Espfs index route handler
 */
cwhttpd_status_t frogfs_route_index(cwhttpd_conn_t *conn);

#ifdef __cplusplus
} /* extern "C" */
#endif
