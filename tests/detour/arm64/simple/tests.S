#if __APPLE__
#define MAC_PAGE @PAGE
#define MAC_PAGEOFF @PAGEOFF
#else
#define MAC_PAGE
#define MAC_PAGEOFF
#endif

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
  adrp x9, msg MAC_PAGE
  mov w10, #0
  mov w11, #1
  strb w11, [x9, msg MAC_PAGEOFF]
  ldrb w12, [x9, msg MAC_PAGEOFF]
  cmp w12, w11
  b d
  strb w10, [x9, msg MAC_PAGEOFF]
 d:
  ret

.global mov2
.align 4
.balign 128
mov2:
  cbz w0, mov2_zero
  ret

mov2_zero:
  adrp x9, msg2 MAC_PAGE
  mov w11, #1
  strb w11, [x9, msg2 MAC_PAGEOFF]
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
  adrp x9, msg3 MAC_PAGE
  mov w11, #1
  strb w11, [x9, msg3 MAC_PAGEOFF]
  mov x0, x9
  ret


.global mov4
.align 4
.balign 128
mov4:
  adrp x9, msg4 MAC_PAGE
  mov w10, #0
  strb w10, [x9, msg4 MAC_PAGEOFF]
  b mov4_zero
  mov w11, #1
  strb w11, [x9, msg4 MAC_PAGEOFF]
  strb w11, [x9, msg4 MAC_PAGEOFF]
  strb w11, [x9, msg4 MAC_PAGEOFF]
  strb w11, [x9, msg4 MAC_PAGEOFF]
  strb w11, [x9, msg4 MAC_PAGEOFF]
  strb w11, [x9, msg4 MAC_PAGEOFF]
  ret

mov4_zero:
  mov w11, #1
  strb w11, [x9, msg4 MAC_PAGEOFF]
  ret

.global mov5
.align 4
.balign 128
mov5:
  adrp x9, msg5 MAC_PAGE
  mov w10, #0
  strb w10, [x9, msg5 MAC_PAGEOFF]
  b mov5_zero
  mov w11, #1
  strb w11, [x9, msg5 MAC_PAGEOFF]

mov5_zero:
  mov w11, #1
  strb w11, [x9, msg5 MAC_PAGEOFF]
  strb w11, [x9, msg5 MAC_PAGEOFF]
  strb w11, [x9, msg5 MAC_PAGEOFF]
  strb w11, [x9, msg5 MAC_PAGEOFF]
  strb w11, [x9, msg5 MAC_PAGEOFF]
  strb w11, [x9, msg5 MAC_PAGEOFF]
  ret
