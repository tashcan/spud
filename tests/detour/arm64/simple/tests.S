.data
.global msg
.global msg2
.global msg3
.global msg4
.global msg5
.global msg6
.global msg7

msg:   .byte 0
msg2:  .byte 0
msg3:  .byte 0
msg4:  .byte 0
msg5:  .byte 0
msg6:  .byte 0
msg7:  .byte 0

.text

.global mov1
.align 4
.balign 128
mov1:
  adrp x9, msg@PAGE
  mov w10, #0
  mov w11, #1
  strb w11, [x9, msg@PAGEOFF]
  ldrb w12, [x9, msg@PAGEOFF]
  cmp w12, w11
  b d
  strb w10, [x9, msg@PAGEOFF]
 d:
  ret

.global mov2
.align 4
.balign 128
mov2:
  cbz w0, mov2_zero
  ret

mov2_zero:
  adrp x9, msg2@PAGE
  mov w11, #1
  strb w11, [x9, msg2@PAGEOFF]
  mov x0, x9
  ret

.global mov3
.align 4
.balign 128
mov3:
  cbz w0, mov3_zero
  ret
  nop
mov3_zero:
  adrp x9, msg3@PAGE
  mov w11, #1
  strb w11, [x9, msg3@PAGEOFF]
  mov x0, x9
  ret


.global mov4
.align 4
.balign 128
mov4:
  adrp x9, msg4@PAGE
  mov w10, #0
  strb w10, [x9, msg4@PAGEOFF]
  b mov4_zero
  mov w11, #1
  strb w11, [x9, msg4@PAGEOFF]
  strb w11, [x9, msg4@PAGEOFF]
  strb w11, [x9, msg4@PAGEOFF]
  strb w11, [x9, msg4@PAGEOFF]
  strb w11, [x9, msg4@PAGEOFF]
  strb w11, [x9, msg4@PAGEOFF]
  ret

mov4_zero:
  mov w11, #1
  strb w11, [x9, msg4@PAGEOFF]
  ret

.global mov5
.align 4
.balign 128
mov5:
  adrp x9, msg5@PAGE
  mov w10, #0
  strb w10, [x9, msg5@PAGEOFF]
  b mov5_zero
  mov w11, #1
  strb w11, [x9, msg5@PAGEOFF]

mov5_zero:
  mov w11, #1
  strb w11, [x9, msg5@PAGEOFF]
  strb w11, [x9, msg5@PAGEOFF]
  strb w11, [x9, msg5@PAGEOFF]
  strb w11, [x9, msg5@PAGEOFF]
  strb w11, [x9, msg5@PAGEOFF]
  strb w11, [x9, msg5@PAGEOFF]
  ret
