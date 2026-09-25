.code16
.intel_syntax noprefix
.global _start

_start:
    mov cx, 10000

again:
    nop
    loop again

    hlt
