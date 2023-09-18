#include <dirent.h>
#include <limits.h>
#include <stdio.h>

#include "cJSON.h"

#include "cwhttpd/httpd.h"
#include "frogfs/format.h"


#define MAX_DEPTH 16
#define CHUNK_SIZE 1024U


#define MIN(a, b) ({ \
    __typeof__(a) _a = a; \
    __typeof__(b) _b = b; \
    _a < _b ? _a : _b; \
})


static cwhttpd_status_t error(cwhttpd_conn_t *conn, const char *s)
{
    cwhttpd_response(conn, 500);
    cwhttpd_send_header(conn, "Content-Type", "text/html");
    cwhttpd_sendf(conn, "<h1>%s</h1>", s);
    return CWHTTPD_STATUS_DONE;
}

static cwhttpd_status_t api_copy(cwhttpd_conn_t *conn)
{
    char *arg, *src = NULL, *dst = NULL, *buf = NULL;
    size_t len, len2;
    FILE *fi, *fo;
    cJSON *root;
    struct stat st;

    arg = malloc(PATH_MAX);
    if (arg == NULL) {
        error(conn, "Could not allocate space for 'arg'");
        goto done;
    }

    len = PATH_MAX;
    if (cwhttpd_find_param("src", conn->request.args, arg, &len) < 0) {
        error(conn, "Argument 'src' not provided");
        goto done;
    }
    if (asprintf(&src, "%s%s", (const char *) conn->route->argv[0], arg) < 0) {
        error(conn, "Could not allocate space for 'src'");
        goto done;
    }

    len = PATH_MAX;
    if (cwhttpd_find_param("dst", conn->request.args, arg, &len) < 0) {
        error(conn, "Argument 'dst' not provided");
        goto done;
    }
    if (asprintf(&dst, "%s%s", (const char *) conn->route->argv[0], arg) < 0) {
        error(conn, "Could not allocate space for 'dst'");
        goto done;
    }

    buf = malloc(CHUNK_SIZE);
    if (buf == NULL) {
        error(conn, "Could not allocate space for 'buf'");
        goto done;
    }

    fi = fopen(src, "rb");
    if (fi == NULL) {
        error(conn, "Could not open input file");
        goto done;
    }

    fo = fopen(dst, "wb");
    if (fo == NULL) {
        fclose(fi);
        error (conn, "Could not open output file");
        goto done;
    }

    while ((len = fread(buf, 1, CHUNK_SIZE, fi)) != 0) {
        len2 = 0;
        while (len2 < len) {
            len2 += fwrite(buf + len2, 1, len - len2, fo);
        }
    }

    fclose(fo);
    fclose(fi);

    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Content-Type", "application/json");

    len = strlen(conn->route->argv[0]);

    root = cJSON_CreateObject();
    if (stat(dst, &st) == 0) {
        cJSON_AddItemToObject(root, "path", cJSON_CreateString(&dst[len]));
        cJSON_AddNumberToObject(root, "size", st.st_size);
        if (st.st_spare4[0] == FROGFS_MAGIC) {
            cJSON_AddTrueToObject(root, "frogfs");
        }
    }
    cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
    cJSON_Delete(root);

done:
    free(arg);
    free(src);
    free(dst);
    free(buf);

    return CWHTTPD_STATUS_DONE;
}

static cwhttpd_status_t api_delete(cwhttpd_conn_t *conn)
{
    char *arg, *path = NULL;
    size_t len;
    cJSON *root;
    struct stat st;

    arg = malloc(PATH_MAX);
    if (arg == NULL) {
        error(conn, "Could not allocate space for 'arg'");
        goto done;
    }

    if (cwhttpd_find_param("path", conn->request.args, arg, &len) < 0) {
        error(conn, "Argument 'path' not provided");
        goto done;
    }
    if (asprintf(&path, "%s%s", (const char *) conn->route->argv[0], arg) < 0) {
        error(conn, "Could not allocate space for 'path'");
        goto done;
    }

    if (unlink(path) < 0) {
        error(conn, "Could not delete file");
        goto done;
    }

    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Content-Type", "application/json");

    len = strlen(conn->route->argv[0]);

    root = cJSON_CreateObject();
    if (stat(path, &st) == 0) {
        cJSON_AddItemToObject(root, "path", cJSON_CreateString(&path[len]));
        cJSON_AddNumberToObject(root, "size", st.st_size);
        if (st.st_spare4[0] == FROGFS_MAGIC) {
            cJSON_AddTrueToObject(root, "frogfs");
        }
    }
    cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
    cJSON_Delete(root);

done:
    free(arg);
    free(path);

    return CWHTTPD_STATUS_DONE;
}

static cwhttpd_status_t api_load(cwhttpd_conn_t *conn)
{
    int length, depth = 0;
    int dirlen[MAX_DEPTH];
    DIR *dirs[MAX_DEPTH];
    struct dirent *dirent;
    char path[PATH_MAX];
    struct stat st;
    bool first = true;
    cJSON *root;

    dirlen[0] = strlcpy(path, conn->route->argv[0], sizeof(path));
    dirs[0] = opendir(path);
    if (dirs[0] == NULL) {
        return error(conn, "opendir() returned NULL");
    }

    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Content-Type", "application/json");
    cwhttpd_send(conn, "[", -1);
    while (true) {
        dirent = readdir(dirs[depth]);
        if (dirent == NULL) {
            closedir(dirs[depth]);
            if (depth-- == 0) {
                break;
            }
        } else {
            length = dirlen[depth] + strlcpy(path + dirlen[depth],
                    dirent->d_name, sizeof(path) - dirlen[depth]);
            if (dirent->d_type == DT_REG && (stat(path, &st) == 0)) {
                if (!first) {
                    cwhttpd_send(conn, ",", -1);
                }
                first = false;
                root = cJSON_CreateObject();
                cJSON_AddItemToObject(root, "path",
                        cJSON_CreateString(&path[dirlen[0]]));
                cJSON_AddNumberToObject(root, "size", st.st_size);
                if (st.st_spare4[0] == FROGFS_MAGIC) {
                    cJSON_AddTrueToObject(root, "frogfs");
                }
                cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
                cJSON_Delete(root);
            } else if (dirent->d_type == DT_DIR && depth < MAX_DEPTH - 1) {
                dirs[depth + 1] = opendir(path);
                if (dirs[depth + 1] == NULL) {
                    continue;
                }
                depth++;
                dirlen[depth] = length;
            }
        }
    }
    cwhttpd_send(conn, "]", -1);
    return CWHTTPD_STATUS_DONE;
}

static cwhttpd_status_t api_move(cwhttpd_conn_t *conn)
{
    char *arg, *src = NULL, *dst = NULL;
    size_t len;
    cJSON *root;
    struct stat st;

    arg = malloc(PATH_MAX);
    if (arg == NULL) {
        error(conn, "Could not allocate space for 'arg'");
        goto done;
    }

    len = PATH_MAX;
    if (cwhttpd_find_param("src", conn->request.args, arg, &len) < 0) {
        error(conn, "Argument 'src' not provided");
        goto done;
    }
    if (asprintf(&src, "%s%s", (const char *) conn->route->argv[0], arg) < 0) {
        error(conn, "Could not allocate space for 'src'");
        goto done;
    }

    len = PATH_MAX;
    if (cwhttpd_find_param("dst", conn->request.args, arg, &len) < 0) {
        error(conn, "Argument 'dst' not provided");
        goto done;
    }
    if (asprintf(&dst, "%s%s", (const char *) conn->route->argv[0], arg) < 0) {
        error(conn, "Could not allocate space for 'dst'");
        goto done;
    }

    if (rename(src, dst) != 0) {
        return error(conn, "Unable to move file");
    }

    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Content-Type", "application/json");

    len = strlen(conn->route->argv[0]);

    cwhttpd_send(conn, "[", -1);

    if (stat(src, &st) == 0) {
        root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "path", cJSON_CreateString(&src[len]));
        cJSON_AddNumberToObject(root, "size", st.st_size);
        if (st.st_spare4[0] == FROGFS_MAGIC) {
            cJSON_AddTrueToObject(root, "frogfs");
        }
        cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
        cwhttpd_send(conn, ",", -1);
        cJSON_Delete(root);
    }

    stat(dst, &st);
    root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "path", cJSON_CreateString(&dst[len]));
    cJSON_AddNumberToObject(root, "size", st.st_size);
    cJSON_DeleteItemFromObject(root, "frogfs");
    cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
    cJSON_Delete(root);

    cwhttpd_send(conn, "]", -1);

done:
    free(arg);
    free(src);
    free(dst);

    return CWHTTPD_STATUS_DONE;
}

static cwhttpd_status_t api_truncate(cwhttpd_conn_t *conn)
{
    char *arg, *path = NULL, *buf = NULL;
    size_t len, len2, length;
    FILE *fi, *fo;
    cJSON *root;
    struct stat st;

    arg = malloc(PATH_MAX);
    if (arg == NULL) {
        error(conn, "Could not allocate space for 'arg'");
        goto done;
    }

    len = PATH_MAX;
    if (cwhttpd_find_param("path", conn->request.args, arg, &len) < 0) {
        error(conn, "Argument 'path' not provided");
        goto done;
    }
    if (asprintf(&path, "%s%s", (const char *) conn->route->argv[0], arg) < 0) {
        error(conn, "Could not allocate space for 'path'");
        goto done;
    }

    len = PATH_MAX;
    if (cwhttpd_find_param("length", conn->request.args, arg, &len) < 0) {
        error(conn, "Argument 'length' not provided");
    }
    length = atoi(arg);

    if (stat(path, &st) == 0) {
        if (st.st_spare4[0] == FROGFS_MAGIC) {
            buf = malloc(CHUNK_SIZE);
            if (buf == NULL) {
                error(conn, "Could not allocate space for 'buf'");
                goto done;
            }

            fi = fopen(path, "rb");
            if (fi == NULL) {
                error(conn, "Could not open input file");
                goto done;
            }

            fo = fopen(path, "wb");
            if (fo == NULL) {
                fclose(fi);
                error (conn, "Could not open output file");
                goto done;
            }

            while ((len = fread(buf, 1, MIN(length, CHUNK_SIZE), fi)) != 0) {
                len2 = 0;
                while (len2 < len) {
                    len2 += fwrite(buf + len2, 1, len - len2, fo);
                }
                length -= len;
                if (length == 0) {
                    break;
                }
            }

            fclose(fo);
            fclose(fi);

            goto skip_truncate;
        }
    }
    truncate(path, length);

skip_truncate:
    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Content-Type", "application/json");

    len = strlen(conn->route->argv[0]);

    root = cJSON_CreateObject();
    if (stat(path, &st) == 0) {
        cJSON_AddItemToObject(root, "path", cJSON_CreateString(&path[len]));
        cJSON_AddNumberToObject(root, "size", st.st_size);
        if (st.st_spare4[0] == FROGFS_MAGIC) {
            cJSON_AddTrueToObject(root, "frogfs");
        }
    }
    cwhttpd_send(conn, cJSON_PrintUnformatted(root), -1);
    cJSON_Delete(root);

done:
    free(arg);
    free(path);
    free(buf);

    return CWHTTPD_STATUS_DONE;
}

static cwhttpd_status_t api_edit(cwhttpd_conn_t *conn)
{
    return CWHTTPD_STATUS_NOTFOUND;
}

static cwhttpd_status_t api_new(cwhttpd_conn_t *conn)
{
    return CWHTTPD_STATUS_NOTFOUND;
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
    } else if (conn->request.method != CWHTTPD_METHOD_POST) {
        if (cwhttpd_find_param("action", conn->request.args, action,
                &len) < 0) {
            error(conn, "Argument 'action' not provided");
            goto done;
        }
        if (strcmp("edit", action) == 0) {
            return api_edit(conn);
        } else if (strcmp("new", action) == 0) {
            return api_new(conn);
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
