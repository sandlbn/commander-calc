/* file_io_ovl.h — giving an overlay its own copy of the file layer.
 *
 * An overlay cannot call another overlay: loading the second would unload
 * the code making the call. The file layer lives in OVL_FILEIO, so every
 * other overlay that touches the card needs its own copy under its own
 * names, compiled into its own area.
 *
 * The duplication is ~2.1 KB of a 7936-byte overlay, which is affordable.
 * It is also temporary: once the assembly reclaim frees enough resident
 * space for the file layer to live there, every copy collapses into one and
 * this header goes away. Until then, include it before file_io.h in any
 * overlay module that opens a file, and add a matching *_io_x16.c.
 *
 * On the host there is one flat program and no overlays, so this is inert.
 */
#ifndef X16S_FILE_IO_OVL_H
#define X16S_FILE_IO_OVL_H

#ifdef __CC65__
#  ifdef FILEIO_OWNER_XSHEET
#    define file_open_read   hfio_open_read
#    define file_open_write  hfio_open_write
#    define file_read        hfio_read
#    define file_write       hfio_write
#    define file_close       hfio_close
#    define file_eof         hfio_eof
#    define file_seek        hfio_seek
#    define file_size        hfio_size
#    define file_remove      hfio_remove
#    define file_rename      hfio_rename
#  endif
#  ifdef FILEIO_OWNER_XSAVE
#    define file_open_read   xfio_open_read
#    define file_open_write  xfio_open_write
#    define file_read        xfio_read
#    define file_write       xfio_write
#    define file_close       xfio_close
#    define file_eof         xfio_eof
#    define file_seek        xfio_seek
#    define file_size        xfio_size
#    define file_remove      xfio_remove
#    define file_rename      xfio_rename
#  endif
#  ifdef FILEIO_OWNER_SAVE
#    define file_open_read   sfio_open_read
#    define file_open_write  sfio_open_write
#    define file_read        sfio_read
#    define file_write       sfio_write
#    define file_close       sfio_close
#    define file_eof         sfio_eof
#    define file_seek        sfio_seek
#    define file_size        sfio_size
#    define file_remove      sfio_remove
#    define file_rename      sfio_rename
#  endif
#  ifdef FILEIO_OWNER_CHART
#    define file_open_read   cfio_open_read
#    define file_open_write  cfio_open_write
#    define file_read        cfio_read
#    define file_write       cfio_write
#    define file_close       cfio_close
#    define file_eof         cfio_eof
#    define file_seek        cfio_seek
#    define file_size        cfio_size
#    define file_remove      cfio_remove
#    define file_rename      cfio_rename
#  endif
#  ifdef FILEIO_OWNER_INF
#    define file_open_read   infio_open_read
#    define file_open_write  infio_open_write
#    define file_read        infio_read
#    define file_write       infio_write
#    define file_close       infio_close
#    define file_eof         infio_eof
#    define file_seek        infio_seek
#    define file_size        infio_size
#    define file_remove      infio_remove
#    define file_rename      infio_rename
#  endif
#  ifdef FILEIO_OWNER_ZIP
#    define file_open_read   zipio_open_read
#    define file_open_write  zipio_open_write
#    define file_read        zipio_read
#    define file_write       zipio_write
#    define file_close       zipio_close
#    define file_eof         zipio_eof
#    define file_seek        zipio_seek
#    define file_size        zipio_size
#    define file_remove      zipio_remove
#    define file_rename      zipio_rename
#  endif
#  ifdef FILEIO_OWNER_CSV
#    define file_open_read   csvio_open_read
#    define file_open_write  csvio_open_write
#    define file_read        csvio_read
#    define file_write       csvio_write
#    define file_close       csvio_close
#    define file_eof         csvio_eof
#    define file_seek        csvio_seek
#    define file_size        csvio_size
#    define file_remove      csvio_remove
#    define file_rename      csvio_rename
#  endif
#endif

#endif /* X16S_FILE_IO_OVL_H */
