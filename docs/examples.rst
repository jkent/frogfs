********
Examples
********

VFS Usage
=========

This is the most common usage. The below example allows access to files using
stdio functions. The filesystem is accessable at the */html* mount point.

.. code-block:: c

   #include <assert.h>
   #include <esp_err.h>

   #include "libespfs/espfs.h"
   #include "libespfs/vfs.h"


   extern const uint8_t espfs_bin[];

   void setup_espfs(void)
   {
        espfs_config_t espfs_config = {
            .addr = espfs_bin,
        };
        espfs_fs_t *espfs = espfs_init(&espfs_config);
        assert(espfs != NULL);

        esp_vfs_espfs_conf_t esp_vfs_espfs_conf = {
            .base_path = "/html",
            .espfs = espfs,
            .max_files = 2,
        };
        ESP_ERROR_CHECK(esp_vfs_espfs_register(&esp_vfs_espfs_conf));
   }
