.intel_syntax noprefix

.data
.global msg
.global msg2
.global msg3
.global msg4
.global msg5
.global msg6
.global msg7

msg:     .byte 0
msg2:    .byte 0
msg3:    .byte 0
msg4:    .byte 0
msg5:    .byte 0
msg6:    .byte 0
msg7:    .byte 0

.text

.global mov1
.align	4
mov1:
  mov byte ptr msg[rip], 0x01
  cmp byte ptr msg[rip], 1
  jmp d
  mov byte ptr msg[rip], 0x0
 d:
  ret


.global mov2
.align 4
mov2:
  test edi, edi
  jz mov2_zero
  ret
 mov2_zero:
  mov byte ptr msg2[rip], 1
  ret

.global mov3
.align 4
mov3:
  endbr64
  test edi, edi
  jz mov3_zero
  ret
  nop dword ptr[rax+0x0]
 mov3_zero:
  mov byte ptr msg3[rip], 1
  ret

.global mov4
.align 4
mov4:
  mov byte ptr msg4[rip], 0
  jmp mov4_zero
  mov byte ptr msg4[rip], 1
  mov byte ptr msg4[rip], 1
  mov byte ptr msg4[rip], 1
  mov byte ptr msg4[rip], 1
  mov byte ptr msg4[rip], 1
  mov byte ptr msg4[rip], 1
  ret
 mov4_zero:
  mov byte ptr msg4[rip], 1
  ret

.global mov5
.align 4
mov5:
  mov byte ptr msg5[rip], 0
  jmp mov5_zero
  mov byte ptr msg5[rip], 1
 mov5_zero:
  mov byte ptr msg5[rip], 1
  mov byte ptr msg5[rip], 1
  mov byte ptr msg5[rip], 1
  mov byte ptr msg5[rip], 1
  mov byte ptr msg5[rip], 1
  mov byte ptr msg5[rip], 1
  ret

.global jz1
.align 4
jz1:
  test    edi, edi
  jz skip
  ret
skip:
   mov byte ptr msg6[rip], 1
   nop
   nop
   nop
   nop
   nop
   nop
   ret

.global jz2
.align 4
jz2:
  push        rax
  mov         dword ptr [rsp+4],edi
  cmp         dword ptr [rsp+4],0
  jne         jz2_cond
  mov         byte ptr msg7[rip],1
  jmp         jz2_exit
jz2_cond:
  jmp         jz2_exit  
jz2_exit:
  pop         rax
  ret