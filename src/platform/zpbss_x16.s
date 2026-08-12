;
; zpbss_x16.s — where the zero-page variables are, and how many.
;
; The linker knows __ZPBSS_RUN__ and __ZPBSS_SIZE__; C cannot ask it
; directly, because cc65 prefixes every C identifier with an underscore and
; an `extern char __ZPBSS_RUN__[]` therefore asks the linker for
; ___ZPBSS_RUN__, which nothing defines. Same reasoning as
; stackfloor_x16.s, and the same fix: compute it here, where the linker's
; own symbols are in scope, and hand it to C as ordinary values.
;
; Hardcoding $0040 would work today and break the moment cc65's runtime
; claims one more byte of zero page.
;

        .export         _zpbss_start, _zpbss_size
        .import         __ZPBSS_RUN__, __ZPBSS_SIZE__

        .rodata

_zpbss_start:
        .word           __ZPBSS_RUN__
_zpbss_size:
        .word           __ZPBSS_SIZE__
