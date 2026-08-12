/* blob_ovl.h — give an overlay its own copy of the blob layer.
 *
 * Same arrangement as file_io_ovl.h: an overlay cannot call another
 * overlay, so what three of them need is duplicated rather than made
 * resident. About 1200 bytes in each of three overlays buys back 1216
 * resident ones.
 *
 * THE CONSEQUENCE FOR CALLERS: nothing outside an overlay may touch a blob.
 * blob_alloc releases whatever the target held and inflate_blob releases
 * its source, so a driver can sequence the phases without freeing one.
 */
#ifndef X16S_BLOB_OVL_H
#define X16S_BLOB_OVL_H

#ifdef __CC65__
#  ifdef BLOB_OWNER_XSAVE
#    define blob_alloc        wblob_alloc
#    define blob_free         wblob_free
#    define blob_reset_write  wblob_reset_write
#    define blob_reset_read   wblob_reset_read
#    define blob_write        wblob_write
#    define blob_read         wblob_read
#    define blob_feed         wblob_feed
#  endif
#  ifdef BLOB_OWNER_ZIP
#    define blob_alloc        zblob_alloc
#    define blob_free         zblob_free
#    define blob_reset_write  zblob_reset_write
#    define blob_reset_read   zblob_reset_read
#    define blob_write        zblob_write
#    define blob_read         zblob_read
#    define blob_feed         zblob_feed
#  endif
#  ifdef BLOB_OWNER_INF
#    define blob_alloc        iblob_alloc
#    define blob_free         iblob_free
#    define blob_reset_write  iblob_reset_write
#    define blob_reset_read   iblob_reset_read
#    define blob_write        iblob_write
#    define blob_read         iblob_read
#    define blob_feed         iblob_feed
#  endif
#  ifdef BLOB_OWNER_STYLE
#    define blob_alloc        yblob_alloc
#    define blob_free         yblob_free
#    define blob_reset_write  yblob_reset_write
#    define blob_reset_read   yblob_reset_read
#    define blob_write        yblob_write
#    define blob_read         yblob_read
#    define blob_feed         yblob_feed
#  endif
#  ifdef BLOB_OWNER_SHEET
#    define blob_alloc        sblob_alloc
#    define blob_free         sblob_free
#    define blob_reset_write  sblob_reset_write
#    define blob_reset_read   sblob_reset_read
#    define blob_write        sblob_write
#    define blob_read         sblob_read
#    define blob_feed         sblob_feed
#  endif
#  ifdef BLOB_OWNER_XLSX
#    define blob_alloc        xblob_alloc
#    define blob_free         xblob_free
#    define blob_reset_write  xblob_reset_write
#    define blob_reset_read   xblob_reset_read
#    define blob_write        xblob_write
#    define blob_read         xblob_read
#    define blob_feed         xblob_feed
#  endif
#endif

#endif /* X16S_BLOB_OVL_H */
