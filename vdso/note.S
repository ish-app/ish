.pushsection .note.Linux, "",@note
    .balign 4
    .long after_name - name // namesz
    .long after_note - note // descz
    .long 0
name:
    .asciz "Linux"
after_name:
    .balign 4
note:
    .long (4 << 16) | (20 << 8) | 69 // 4.20.69
after_note:
    .balign 4
.popsection
