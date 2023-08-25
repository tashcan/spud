.data
PUBLIC msg
PUBLIC msg2
PUBLIC msg3
msg  byte 0
msg2 byte 0
msg3 byte 0

.code
mov1 proc
  mov byte ptr [offset msg], 1
  cmp byte ptr [offset msg], 1
  jmp d
  mov byte ptr [offset msg], 0
 d:
  ret
mov1 endp

mov2 proc
 test ecx, ecx
 jz mov2_zero
 ret
mov2_zero:
 mov byte ptr[offset msg2], 1
 ret
mov2 endp

mov3 proc
  endbr64
  test ecx, ecx
  jz mov3_zero
  ret
  nop dword ptr [rax + 00h]
 mov3_zero:
  mov byte ptr [offset msg3], 1
  ret
mov3 endp


end

