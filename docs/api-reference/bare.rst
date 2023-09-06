Bare API
========

`frogfs/frogfs.h`

Defines
^^^^^^^

.. doxygendefine:: FROGFS_OPEN_RAW

Functions
^^^^^^^^^

.. doxygenfunction:: frogfs_init
.. doxygenfunction:: frogfs_deinit
.. doxygenfunction:: frogfs_obj_from_path
.. doxygenfunction:: frogfs_path_from_obj
.. doxygenfunction:: frogfs_stat
.. doxygenfunction:: frogfs_open
.. doxygenfunction:: frogfs_close
.. doxygenfunction:: frogfs_read
.. doxygenfunction:: frogfs_seek
.. doxygenfunction:: frogfs_tell
.. doxygenfunction:: frogfs_access
.. doxygenfunction:: frogfs_opendir
.. doxygenfunction:: frogfs_closedir
.. doxygenfunction:: frogfs_readdir
.. doxygenfunction:: frogfs_rewinddir
.. doxygenfunction:: frogfs_seekdir
.. doxygenfunction:: frogfs_telldir

Structs
^^^^^^^

.. doxygenstruct:: frogfs_config_t
    :members:
.. doxygenstruct:: frogfs_decomp_t
    :members:
.. doxygenstruct:: frogfs_fs_t
    :members:
.. doxygenstruct:: frogfs_stat_t
    :members:
.. doxygenstruct:: frogfs_f_t
    :members:
.. doxygenstruct:: frogfs_d_t
    :members:
.. doxygenstruct:: frogfs_decomp_funcs_t
    :members:

Variables
^^^^^^^^^

.. doxygenvariable:: frogfs_decomp_raw
.. doxygenvariable:: frogfs_decomp_deflate
.. doxygenvariable:: frogfs_decomp_heatshrink
