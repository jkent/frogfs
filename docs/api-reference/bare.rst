Bare API
========

`frogfs/frogfs.h`

Defines
^^^^^^^

.. doxygendefine:: FROGFS_MAGIC
.. doxygendefine:: FROGFS_VER_MAJOR
.. doxygendefine:: FROGFS_VER_MINOR
.. doxygendefine:: FROGFS_OPEN_RAW

Functions
^^^^^^^^^

.. doxygenfunction:: frogfs_init
.. doxygenfunction:: frogfs_deinit
.. doxygenfunction:: frogfs_get_entry
.. doxygenfunction:: frogfs_get_name
.. doxygenfunction:: frogfs_get_path
.. doxygenfunction:: frogfs_is_dir
.. doxygenfunction:: frogfs_is_file
.. doxygenfunction:: frogfs_stat
.. doxygenfunction:: frogfs_open
.. doxygenfunction:: frogfs_close
.. doxygenfunction:: frogfs_is_raw
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

Enums
^^^^^

.. doxygenenum:: frogfs_entry_type_t
.. doxygenenum:: frogfs_comp_algo_t

Typedefs
^^^^^^^^

.. doxygentypedef:: frogfs_fs_t
.. doxygentypedef:: frogfs_entry_t

Structs
^^^^^^^

.. doxygenstruct:: frogfs_config_t
    :members:
.. doxygenstruct:: frogfs_stat_t
    :members:
.. doxygenstruct:: frogfs_fh_t
    :members:
.. doxygenstruct:: frogfs_dh_t
    :members:
