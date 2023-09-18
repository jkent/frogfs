#include "esp_err.h"

#include "cwhttpd/httpd.h"
#include "cwhttpd/route.h"
#include "frogfs/frogfs.h"


extern frogfs_fs_t *frogfs;
extern cwhttpd_status_t cwhttpd_route_api(cwhttpd_conn_t *conn);

esp_err_t httpd_start(void)
{
    cwhttpd_inst_t *inst = cwhttpd_init(NULL, CWHTTPD_FLAG_NONE);
    inst->frogfs = frogfs;

    cwhttpd_route_append(inst, "/browser/api", cwhttpd_route_api, 1, "/frogfs");
    cwhttpd_route_append(inst, "*", cwhttpd_route_fs_get, 1, "/frogfs/");
    return cwhttpd_start(inst) ? ESP_OK : ESP_FAIL;
}
