********
Examples
********

VFS Usage
=========

This is the most common usage. The below example allows access to files using
stdio functions. Files are mounted in the */html* directory.

.. code-block:: c

   #inclue <assert.h>
   #include <esp_err.h>

   #include "libespfs/espfs.h"


   void setup_espfs(void)
   {
        espfs_config_t espfs_config = {
            .addr = espfsimage_bin,
        };
        espfs_t* espfs = espfs_init(&espfs_config);
        assert(espfs != NULL);

        esp_vfs_espfs_conf_t esp_vfs_espfs_conf = {
            .base_path = "/html",
            .espfs = espfs,
            .max_files = 2,
        };
        ESP_ERROR_CHECK(esp_vfs_espfs_register(&esp_vfs_espfs_conf));
   }
