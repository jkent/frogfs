#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "frogfs/vfs.h"


/* Scratch buffer size */
#define SCRATCH_BUFSIZE  512

struct file_server_data {
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};

static const char *TAG = "http_server";

static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath)
{
    char entrypath[PATH_MAX];
    struct dirent *entry;
    struct stat entry_stat;
    char buf[2048];

    DIR *dir = opendir(dirpath);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                "Directory does not exist");
        return ESP_FAIL;
    }

    const size_t dirpath_len = strlen(dirpath);

    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));
    strlcpy(entrypath + dirpath_len, "/", sizeof(entrypath) - dirpath_len);

    /* Send HTML file header */
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html>\n");
    httpd_resp_sendstr_chunk(req, "<head><title>Index of ");
    httpd_resp_sendstr_chunk(req, req->uri);
    httpd_resp_sendstr_chunk(req, "</title></head>\n");
    httpd_resp_sendstr_chunk(req, "<h1>Index of ");
    httpd_resp_sendstr_chunk(req, req->uri);
    httpd_resp_sendstr_chunk(req, "</h1><hr><pre>\n");
    if (strcmp(req->uri, "/") != 0) {
        httpd_resp_sendstr_chunk(req, "<a href=\"../\">../</a>\n");
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) {
            continue;
        }

        int len = strlen(entry->d_name);

        snprintf(buf, sizeof(buf), "<a href=\"%s%s/\">%s/</a>%*s%19s</a>\n",
               req->uri, entry->d_name, entry->d_name, 68 - len, "", "-");
        httpd_resp_sendstr_chunk(req, buf);
    }

    rewinddir(dir);
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            continue;
        }

        strlcpy(entrypath + dirpath_len + 1, entry->d_name,
                sizeof(entrypath) - dirpath_len - 1);
        if (stat(entrypath, &entry_stat) < 0) {
            ESP_LOGE(TAG, "Failed to stat %s", entry->d_name);
            continue;
        }

        int len = strlen(entry->d_name);

        snprintf(buf, sizeof(buf), "<a href=\"%s%s\">%s</a>%*s%19ld\n",
                req->uri, entry->d_name, entry->d_name, 69 - len, "",
                entry_stat.st_size);
        httpd_resp_sendstr_chunk(req, buf);
    }
    closedir(dir);

    httpd_resp_sendstr_chunk(req, "</pre><hr></body>\n");
    httpd_resp_sendstr_chunk(req, "</html>\n");

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req,
        const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, "text/html");
    } else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    }
    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}

/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char* get_path_from_uri(char *dest, const char *base_path,
        const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

static bool remove_trailing_slashes(char *dest)
{
    size_t destlen = strlen(dest);
    bool removed = false;

    while (destlen > 0 && dest[destlen - 1] == '/') {
        dest[--destlen] = '\0';
        removed = true;
    }

    return removed;
}

/* Handler to download a file kept on the server */
static esp_err_t download_get_handler(httpd_req_t *req)
{
    char filepath[PATH_MAX];
    FILE *fd = NULL;
    struct stat st;

    const char *filename = get_path_from_uri(filepath,
            ((struct file_server_data *)req->user_ctx)->base_path, req->uri,
            sizeof(filepath));
    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                "Filename too long");
        return ESP_FAIL;
    }

    bool had_slash = remove_trailing_slashes(filepath);

    int st_ret = stat(filepath, &st);
    if (st_ret < 0) {
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    if (st.st_mode & S_IFDIR) {
        if (!had_slash) {
            size_t uri_len = strlen(req->uri);
            if (uri_len + 1 > HTTPD_MAX_URI_LEN) {
                ESP_LOGE(TAG, "URI is too long");
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "URI is too long");
                return ESP_FAIL;
            }
            char uri[HTTPD_MAX_URI_LEN + 1];
            memcpy(uri, req->uri, uri_len);
            uri[uri_len] = '/';
            uri[uri_len + 1] = '\0';
            httpd_resp_set_status(req, "307 Temporary Redirect");
            httpd_resp_set_hdr(req, "Location", uri);
            httpd_resp_send(req, NULL, 0);  // Response body can be empty
            return ESP_OK;
        }

        size_t len = strlen(filepath);
        if (len + 12 < PATH_MAX) {
            char index[PATH_MAX];
            struct stat st2;

            strcpy(index, filepath);
            strcpy(index + len, "/index.html");
            if (stat(index, &st2) >= 0 && st2.st_mode & S_IFREG) {
                strcpy(filepath, index);
                memcpy(&st, &st2, sizeof(st));
                st_ret = 0;
                goto found;
            }
        }

        return http_resp_dir_html(req, filepath);
    }

found:
    char buf[256];
    bool passthrough_deflate = false;

    if (st.st_spare4[0] == FROGFS_MAGIC &&
            (st.st_spare4[1] & 0xFF) == FROGFS_COMP_DEFLATE) {
        httpd_req_get_hdr_value_str(req, "Accept-Encoding", buf, sizeof(buf));
        if (strstr(buf, "deflate")) {
            passthrough_deflate = true;
        }
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read existing file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                "Failed to read existing file");
        return ESP_FAIL;
    }

    if (passthrough_deflate && fcntl(fileno(fd), F_REOPEN_RAW) >= 0) {
        fstat(fileno(fd), &st);
        httpd_resp_set_hdr(req, "Content-Encoding", "deflate");
    }

    ESP_LOGI(TAG, "Sending file : %s (%ld bytes)...", filename, st.st_size);
    set_content_type_from_file(req, filename);

    /* Retrieve the pointer to scratch buffer for temporary storage */
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);

        if (chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to send file");
               return ESP_FAIL;
           }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);

    /* Close file after sending complete */
    fclose(fd);
    ESP_LOGI(TAG, "File sending complete");

    /* Respond with an empty chunk to signal HTTP response completion */
#ifdef CONFIG_EXAMPLE_HTTPD_CONN_CLOSE_HEADER
    httpd_resp_set_hdr(req, "Connection", "close");
#endif
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Function to start the file server */
esp_err_t start_http_server(const char *base_path)
{
    static struct file_server_data *server_data = NULL;

    if (server_data) {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.stack_size = 10240;

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start file server!");
        return ESP_FAIL;
    }

    /* URI handler for getting uploaded files */
    httpd_uri_t file_download = {
        .uri       = "/*",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = download_get_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_download);

    return ESP_OK;
}
