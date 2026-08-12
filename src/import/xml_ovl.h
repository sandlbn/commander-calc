/* xml_ovl.h — giving an overlay its own copy of the XML tokenizer.
 * Same arrangement as blob_ovl.h and file_io_ovl.h.
 */
#ifndef X16S_XML_OVL_H
#define X16S_XML_OVL_H

#ifdef __CC65__
#  ifdef XML_OWNER_STYLE
#    define xml_init  yxml_init
#    define xml_next  yxml_next
#    define xml_is    yxml_is
#  endif
#  ifdef XML_OWNER_SHEET
#    define xml_init  sxml_init
#    define xml_next  sxml_next
#    define xml_is    sxml_is
#  endif
#endif

#endif /* X16S_XML_OVL_H */
