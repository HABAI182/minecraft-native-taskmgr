; Probe the real Taskmgr leaf getter with live values in volatile registers.
; Internal Taskmgr callers depend on this stronger-than-public-ABI contract.
; The removed C++ detour overwrote EDX and R8D during its TLS lookup.
.code
ProbeLeafCount PROC
    push rbx
    push rsi
    push rdi
    sub rsp, 32
    mov rbx, rcx
    mov rsi, rdx
    mov rdi, r8
    mov rcx, rsi
    mov rdx, 1122334455667788h
    mov r8, 2233445566778899h
    mov r9, 33445566778899AAh
    mov r10, 445566778899AABBh
    mov r11, 5566778899AABBCCh
    call rbx
    mov [rdi], rdx
    mov [rdi+8], r8
    mov [rdi+16], r9
    mov [rdi+24], r10
    mov [rdi+32], r11
    add rsp, 32
    pop rdi
    pop rsi
    pop rbx
    ret
ProbeLeafCount ENDP
END
