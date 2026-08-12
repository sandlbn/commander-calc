;
; stackfloor_x16.s — where the C stack runs out.
;
; The linker knows __OVLSTART__ and __STACKSIZE__; C cannot ask it directly,
; because cc65 prefixes every C identifier with an underscore and an
; `extern char __OVLSTART__[]` therefore asks the linker for ___OVLSTART__,
; which nothing defines. That fails as an unresolved external rather than
; silently, but only because the symbol is referenced by name — a
; hand-written constant would have compiled and been wrong the next time the
; configuration moved.
;
; So the address is computed here, where the linker's own symbols are in
; scope, and handed to C as an ordinary pointer.
;

        .export         _stack_floor
        .import         __OVLSTART__, __STACKSIZE__

        .rodata

_stack_floor:
        .word           __OVLSTART__ - __STACKSIZE__
