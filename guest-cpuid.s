.code16
.intel_syntax noprefix
.global _start

_start:
    mov eax, 0
    cpuid
    hlt
