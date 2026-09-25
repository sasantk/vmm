.code16
.intel_syntax noprefix
.global _start

_start:
    mov cx, 10000
    mov al, 42

again:
    out 0xe9, al
    loop again

    hlt
