#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

#include "cJSON.h"

#include "cwhttpd/httpd.h"
#include "frogfs/frogfs.h"
#include "frogfs/format.h"


#define CHUNK_SIZE 1024U

#define MIN(a, b) ({ \
    __typeof__(a) _a = a; \
    __typeof__(b) _b = b; \
    _a < _b ? _a : _b; \
})

static cwhttpd_status_t error(cwhttpd_conn_t *conn, const char *fmt, ...)
{
    va_list va;
    char *s;

    cwhttpd_response(conn, 500);
    cwhttpd_send_header(conn, "Content-Type", "text/html");
    va_start(va, fmt);
    vasprintf(&s, fmt, va);
    va_end(va);
    cwhttpd_sendf(conn, "<h1>%s</h1>", s);
    free(s);
    return CWHTTPD_STATUS_FAIL;
}

typedef struct api_copy_data_t {
    char src[PATH_MAX];
    char dst[PATH_MAX];
    char buf[CHUNK_SIZE];
    int fdi, fdo;
} api_copy_data_t;

static cwhttpd_status_t api_copy(cwhttpd_conn_t *conn)
{
    api_copy_data_t *data = conn->user;
    size_t len, tmp;
    cJSON *root;
    struct stat st;

    if (data == NULL) {
        data = malloc(sizeof(*data));
        if (!data) {
            return error(conn, "Unable to allocate data");
        }

        len = strlcpy(data->src, conn->route->argv[0], sizeof(data->src));
        tmp = sizeof(data->src) - len;
        if (cwhttpd_find_param("src", conn->request.args, data->src + len,
                &tmp) < 0) {
            free(data);
            return error(conn, "Argument 'src' not provided");
        }

        len = strlcpy(data->dst, conn->route->argv[0], sizeof(data->dst));
        tmp = sizeof(data->dst) - len;
        if (cwhttpd_find_param("dst", conn->request.args, data->dst + len,
                &tmp) < 0) {
            free(data);
            return error(conn, "Argument 'dst' not provided");
        }

        conn->user = data;
    }

    data->fdi = open(data->src, O_RDONLY);
    if (data->fdi < 0) {
        free(data);
        return error(conn, "Could not open input file '%s'", data->src);
    }

    data->fdo = open(data->dst, O_WRONLY | O_CREAT | O_TRUNC,
            S_IRWXU | S_IRWXG | S_IRWXO);
    if (data->fdo < 0) {
        close(data->fdi);
        free(data);
        return error(conn, "Could not open output file '%s'", data->dst);
    }

    while (true) {
        ssize_t ret = read(data->fdi, data->buf, sizeof(data->buf));
        if (ret < 0) {
            close(data->fdi);
            close(data->fdo);
            free(data);
            return error(conn, "Error reading input file");
        }
        if (ret == 0) {
            break;
        }

        len = ret;
        tmp = 0;
        while (len > 0) {
            ret = write(data->fdo, data->buf + tmp, len);
            if (ret < 0) {
                close(data->fdi);
                close(data->fdo);
                free(data);
                return error(conn, "Error writing output file");
            }
            len -= ret;
            tmp += ret;
        }
    }

    close(data->fdi);
    close(data->fdo);

    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Content-Type", "application/json");

    root = cJSON_CreateObject();
    if (stat(data->dst, &st) == 0) {
        len = strlen(conn->route->argv[0]);
        cJSON_AddItemToObject(root, "path",
                cJSON_CreateString(&data->dst[len]));
        cJSON_AddNumberToObject(root, "size", st.st_size);
        if (st.st_spare4[0] == FROGFS_MAGIC) {
            cJSON_AddTrueToObject(root, "frogfs");
        }
    }
    cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
    cJSON_Delete(root);

    free(data);

    return CWHTTPD_STATUS_DONE;
}

typedef struct api_delete_data_t {
    char path[PATH_MAX];
} api_delete_data_t;

static cwhttpd_status_t api_delete(cwhttpd_conn_t *conn)
{
    api_delete_data_t *data = conn->user;
    size_t len, tmp;
    cJSON *root;
    struct stat st;

    if (data == NULL) {
        data = malloc(sizeof(*data));
        if (!data) {
            return error(conn, "Unable to allocate data");
        }

        len = strlcpy(data->path, conn->route->argv[0], sizeof(data->path));
        tmp = sizeof(data->path) - len;
        if (cwhttpd_find_param("path", conn->request.args, data->path + len,
                &tmp) < 0) {
            free(data);
            return error(conn, "Argument 'path' not provided");
        }

        conn->user = data;
    }

    if (unlink(data->path) < 0) {
        free(data);
        return error(conn, "Could not delete file");
    }

    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Content-Type", "application/json");

    root = cJSON_CreateObject();
    if (stat(data->path, &st) == 0) {
        len = strlen(conn->route->argv[0]);
        cJSON_AddItemToObject(root, "path",
                cJSON_CreateString(&data->path[len]));
        cJSON_AddNumberToObject(root, "size", st.st_size);
        if (st.st_spare4[0] == FROGFS_MAGIC) {
            cJSON_AddTrueToObject(root, "frogfs");
        }
    }
    cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
    cJSON_Delete(root);

    free(data);
    return CWHTTPD_STATUS_DONE;
}

typedef struct api_load_data_t {
    int dirlen[FROGFS_MAX_FLAT_DEPTH];
    DIR *dirs[FROGFS_MAX_FLAT_DEPTH];
    char path[PATH_MAX];
} api_load_data_t;

static cwhttpd_status_t api_load(cwhttpd_conn_t *conn)
{
    api_load_data_t *data = conn->user;
    int len, depth = 0;
    struct dirent *dirent;
    struct stat st;
    bool first = true;
    cJSON *root;

    if (data == NULL) {
        data = malloc(sizeof(*data));
        if (!data) {
            return error(conn, "Unable to allocate data");
        }

        conn->user = data;
    }

    data->dirlen[0] = strlcpy(data->path, conn->route->argv[0],
            sizeof(data->path));
    data->dirs[0] = opendir(data->path);
    if (data->dirs[0] == NULL) {
        return error(conn, "opendir() returned NULL");
    }

    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Content-Type", "application/json");
    cwhttpd_send(conn, "[", -1);

    while (true) {
        dirent = readdir(data->dirs[depth]);
        if (dirent == NULL) {
            closedir(data->dirs[depth]);
            if (depth-- == 0) {
                break;
            }
        } else {
            len = data->dirlen[depth] + strlcpy(data->path +
                    data->dirlen[depth], dirent->d_name, sizeof(data->path) -
                    data->dirlen[depth]);
            if (dirent->d_type == DT_REG && (stat(data->path, &st) == 0)) {
                if (!first) {
                    cwhttpd_send(conn, ",", -1);
                }
                first = false;
                root = cJSON_CreateObject();
                cJSON_AddItemToObject(root, "path",
                        cJSON_CreateString(&data->path[data->dirlen[0]]));
                cJSON_AddNumberToObject(root, "size", st.st_size);
                if (st.st_spare4[0] == FROGFS_MAGIC) {
                    cJSON_AddTrueToObject(root, "frogfs");
                }
                cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
                cJSON_Delete(root);
            } else if (dirent->d_type == DT_DIR &&
                    depth < FROGFS_MAX_FLAT_DEPTH - 1) {
                data->dirs[depth + 1] = opendir(data->path);
                if (data->dirs[depth + 1] == NULL) {
                    continue;
                }
                depth++;
                data->dirlen[depth] = len;
            }
        }
    }

    cwhttpd_send(conn, "]", -1);
    free(data);

    return CWHTTPD_STATUS_DONE;
}

typedef struct api_move_data_t {
    char src[PATH_MAX];
    char dst[PATH_MAX];
} api_move_data_t;

static cwhttpd_status_t api_move(cwhttpd_conn_t *conn)
{
    api_move_data_t *data = conn->user;
    size_t len, tmp;
    cJSON *root;
    struct stat st;

    if (data == NULL) {
        data = malloc(sizeof(*data));
        if (!data) {
            return error(conn, "Unable to allocate data");
        }

        len = strlcpy(data->src, conn->route->argv[0], sizeof(data->src));
        tmp = sizeof(data->src) - len;
        if (cwhttpd_find_param("src", conn->request.args, data->src + len,
                &tmp) < 0) {
            free(data);
            return error(conn, "Argument 'src' not provided");
        }

        len = strlcpy(data->dst, conn->route->argv[0], sizeof(data->dst));
        tmp = sizeof(data->dst) - len;
        if (cwhttpd_find_param("dst", conn->request.args, data->dst + len,
                &tmp) < 0) {
            free(data);
            return error(conn, "Argument 'dst' not provided");
        }

        conn->user = data;
    }

    if (rename(data->src, data->dst) != 0) {
        return error(conn, "Unable to move file");
    }

    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Content-Type", "application/json");

    cwhttpd_send(conn, "[", -1);

    len = strlen(conn->route->argv[0]);
    if (stat(data->src, &st) == 0) {
        root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "path",
                cJSON_CreateString(&data->src[len]));
        cJSON_AddNumberToObject(root, "size", st.st_size);
        if (st.st_spare4[0] == FROGFS_MAGIC) {
            cJSON_AddTrueToObject(root, "frogfs");
        }
        cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
        cwhttpd_send(conn, ",", -1);
        cJSON_Delete(root);
    }

    stat(data->dst, &st);
    root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "path", cJSON_CreateString(&data->dst[len]));
    cJSON_AddNumberToObject(root, "size", st.st_size);
    cJSON_DeleteItemFromObject(root, "frogfs");
    cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
    cJSON_Delete(root);

    cwhttpd_send(conn, "]", -1);

    free(data);

    return CWHTTPD_STATUS_DONE;
}

typedef struct api_truncate_data_t {
    char path[PATH_MAX];
    char buf[13];
} api_truncate_data_t;

static cwhttpd_status_t api_truncate(cwhttpd_conn_t *conn)
{
    api_truncate_data_t *data = conn->user;
    size_t len = 0, tmp;
    cJSON *root;
    struct stat st;

    if (data == NULL) {
        data = malloc(sizeof(*data));
        if (!data) {
            return error(conn, "Unable to allocate data");
        }

        len = strlcpy(data->path, conn->route->argv[0], sizeof(data->path));
        tmp = sizeof(data->path) - len;
        if (cwhttpd_find_param("path", conn->request.args, data->path + len,
                &tmp) < 0) {
            free(data);
            return error(conn, "Argument 'path' not provided");
        }

        tmp = sizeof(data->buf);
        if (cwhttpd_find_param("length", conn->request.args, data->buf,
                &tmp) < 0) {
            return error(conn, "Argument 'length' not provided");
        }
        len = atoi(data->buf);

        conn->user = data;
    }

    if (truncate(data->path, len) != 0) {
        free(data);
        return error(conn, "Truncate failed");
    };

    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Content-Type", "application/json");

    root = cJSON_CreateObject();
    if (stat(data->path, &st) == 0) {
        len = strlen(conn->route->argv[0]);
        cJSON_AddItemToObject(root, "path",
                cJSON_CreateString(&data->path[len]));
        cJSON_AddNumberToObject(root, "size", st.st_size);
        if (st.st_spare4[0] == FROGFS_MAGIC) {
            cJSON_AddTrueToObject(root, "frogfs");
        }
    }
    cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
    cJSON_Delete(root);

    free(data);

    return CWHTTPD_STATUS_DONE;
}

typedef struct api_edit_data_t {
    char path[PATH_MAX];
    int fd;
} api_edit_data_t;

static cwhttpd_status_t api_edit(cwhttpd_conn_t *conn)
{
    api_edit_data_t *data = conn->user;
    size_t len, tmp;
    cJSON *root;
    struct stat st;

    if (data == NULL) {
        data = malloc(sizeof(*data));
        if (data == NULL) {
            return error(conn, "Could not allocate data");
        }

        len = strlcpy(data->path, conn->route->argv[0], sizeof(data->path));
        tmp = sizeof(data->path) - len;
        if (cwhttpd_find_param("path", conn->request.args, data->path + len,
                &tmp) < 0) {
            free(data);
            return error(conn, "Argument 'path' not provided");
        }

        data->fd = open(data->path, O_WRONLY | O_CREAT | O_TRUNC,
                S_IRWXU | S_IRWXG | S_IRWXO);
        if (data->fd < 0) {
            free(data);
            return error(conn, "Could not open output file: '%s'", data->path);
        }

        conn->user = data;
    }

    size_t pos = 0;
    while (conn->post->buf_len > 0) {
        ssize_t ret = write(data->fd, conn->post->buf + pos,
                conn->post->buf_len);
        if (ret < 0) {
            free(data);
            return error(conn, "Error writing to file");
        }
        conn->post->buf_len -= ret;
        pos += ret;
    }

    if (conn->post->received < conn->post->len) {
        return CWHTTPD_STATUS_MORE;
    }

    close(data->fd);

    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Content-Type", "application/json");

    root = cJSON_CreateObject();
    if (stat(data->path, &st) == 0) {
        len = strlen(conn->route->argv[0]);
        cJSON_AddItemToObject(root, "path",
                cJSON_CreateString(&data->path[len]));
        cJSON_AddNumberToObject(root, "size", st.st_size);
        if (st.st_spare4[0] == FROGFS_MAGIC) {
            cJSON_AddTrueToObject(root, "frogfs");
        }
    }
    cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
    cJSON_Delete(root);

    free(data);

    return CWHTTPD_STATUS_DONE;
}

cwhttpd_status_t cwhttpd_route_api(cwhttpd_conn_t *conn)
{
    char action[10];
    size_t len = 10;

    if (conn->request.method == CWHTTPD_METHOD_GET) {
        if (cwhttpd_find_param("action", conn->request.args, action,
                &len) < 0) {
            error(conn, "Argument 'action' not provided");
            goto done;
        }
        if (strcmp("copy", action) == 0) {
            return api_copy(conn);
        } else if (strcmp("delete", action) == 0) {
            return api_delete(conn);
        } else if (strcmp("load", action) == 0) {
            return api_load(conn);
        } else if (strcmp("move", action) == 0) {
            return api_move(conn);
        } else if (strcmp("truncate", action) == 0) {
            return api_truncate(conn);
        } else {
            cwhttpd_response(conn, 501);
            cwhttpd_send_header(conn, "Content-Type", "text/html");
            cwhttpd_sendf(conn, "<h1>Invalid GET action '%s'</h1>", action);
        }
    } else if (conn->request.method == CWHTTPD_METHOD_POST) {
        if (cwhttpd_find_param("action", conn->request.args, action,
                &len) < 0) {
            error(conn, "Argument 'action' not provided");
            goto done;
        }
        if (strcmp("edit", action) == 0) {
            return api_edit(conn);
        } else if (strcmp("new", action) == 0) {
            return api_edit(conn);
        } else {
            cwhttpd_response(conn, 501);
            cwhttpd_send_header(conn, "Content-Type", "text/html");
            cwhttpd_sendf(conn, "<h1>Invalid POST action '%s'</h1>", action);
        }
    } else {
        cwhttpd_response(conn, 405); // Method not allowed
        cwhttpd_send_header(conn, "Content-Type", "text/html");
        cwhttpd_send(conn, "<h1>Method not allowed</h1>", -1);
    }

done:
    return CWHTTPD_STATUS_DONE;
}
