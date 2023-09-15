.data
PUBLIC msg
PUBLIC msg2
PUBLIC msg3
PUBLIC msg4
PUBLIC msg5
msg  byte 0
msg2 byte 0
msg3 byte 0
msg4 byte 0
msg5 byte 0

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

mov4 proc
  mov byte ptr [offset msg4], 0
  jmp mov4_zero
  mov byte ptr [offset msg4], 1
  mov byte ptr [offset msg4], 2
  mov byte ptr [offset msg4], 3
  mov byte ptr [offset msg4], 4
  mov byte ptr [offset msg4], 5
  mov byte ptr [offset msg4], 1
  ret
 mov4_zero:
  mov byte ptr [offset msg4], 1
  ret
mov4 endp

mov5 proc
  mov byte ptr [offset msg5], 0
  jmp mov5_zero
  mov byte ptr [offset msg5], 6
 mov5_zero:
  mov byte ptr [offset msg5], 1
  mov byte ptr [offset msg5], 2
  mov byte ptr [offset msg5], 3
  mov byte ptr [offset msg5], 4
  mov byte ptr [offset msg5], 5
  mov byte ptr [offset msg5], 1
  ret
mov5 endp

end

