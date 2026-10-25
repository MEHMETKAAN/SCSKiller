; `selftest nvqi` için nvapi64.dll'nin nvapi_QueryInterface fonksiyonu: int3 dolgusu, ardından 2 baytlık ilk komut
; (derleyicinin anında yamanabilir (hot-patchable) girişi olarak push'un REX formu) ve bir sub; FakeNext_NvQiInside bir iş parçacığını ikisi arasında durdurur.
EXTERN fake_nv_qi:PROC
EXTERN fake_nv_qi_inside:PROC
EXTERN FakeNv_OnOpts:PROC
EXTERN fake_ffx_create:PROC
.code
ALIGN 16
    db 16 dup (0CCh)
nvapi_QueryInterface PROC FRAME
    db 40h
    push rbx
    .pushreg rbx
    sub rsp, 32
    .allocstack 32
    .endprolog
    call fake_nv_qi
    add rsp, 32
    pop rbx
    ret
nvapi_QueryInterface ENDP

; nvapi_QueryInterface(id), ilk komutunu burada çalıştıran, fake_nv_qi_inside içinde bekleyen ve ardından ikinci komutuna devam eden bir
; iş parçacığı (thread) tarafından çalıştırılır.
FakeNext_NvQiInside PROC FRAME
    db 40h
    push rbx
    .pushreg rbx
    sub rsp, 32
    .allocstack 32
    .endprolog
    mov [rsp + 48], rcx
    call fake_nv_qi_inside
    mov rcx, [rsp + 48]
    add rsp, 32
    jmp nvapi_QueryInterface + 2
FakeNext_NvQiInside ENDP

; `selftest nvopts` için NvAPI_D3D12_SetCreatePipelineStateOptions: Sürücünün ilk komutu (5 baytlık bir store), ardından
; arayanın (caller) argümanlarıyla FakeNv_OnOpts.
ALIGN 16
FakeNv_SetOpts PROC
    mov [rsp + 18h], rbx
    sub rsp, 28h
    call FakeNv_OnOpts
    add rsp, 28h
    ret
FakeNv_SetOpts ENDP

;
; Satıriçi kancanın (inline hook) yalnız bırakması gereken girişler (proxy'nin kancaladığı üç NVAPI işlevi olarak `selftest nvqi`): 
; 1 baytlık ilk komut, sadece 4 int3'ten sonra 2 baytlık bir komut ve int3 veya ret olmayan bir bayttan (canlı bir komutun 
; ara (immediate) kısmının kuyruğu olabilirler) sonra 5 int3'ün ardındaki bir komut.
ALIGN 16
    db 16 dup (0CCh)
FakeNv_OneByte PROC
    push rbp
    pop rbp
    xor eax, eax
    ret
FakeNv_OneByte ENDP

ALIGN 16
    db 12 dup (90h)
    db 4 dup (0CCh)
FakeNv_ShortPad PROC
    db 40h
    push rbx
    pop rbx
    xor eax, eax
    ret
FakeNv_ShortPad ENDP

ALIGN 16
    db 11 dup (90h)
    db 5 dup (0CCh)
FakeNv_LivePad PROC
    db 40h
    push rbx
    pop rbx
    xor eax, eax
    ret
FakeNv_LivePad ENDP

; Başka bir kancalayıcının proxy onu kancalarken yamaladığı bir giriş (`selftest nvqi`, SCSKILLER_TEST_HOOK_RACE): 
; jmp'nin değiştirdiği 5 baytlık ilk komut.
ALIGN 16
FakeNv_Raced PROC
    mov [rsp + 8], rbx
    xor eax, eax
    ret
FakeNv_Raced ENDP

; `selftest framesfg ffx` için FidelityFX'in ffxCreateContext'i: yükleyicinin anında yama (hot-patch) dolgusu ve 3 baytlık
; ilk komut, ardından fake_ffx_create.
ALIGN 16
    db 16 dup (0CCh)
ffxCreateContext PROC
    db 48h, 8Bh, 0C0h
    jmp fake_ffx_create
ffxCreateContext ENDP
END
