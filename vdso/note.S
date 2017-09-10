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
    .long 0x030200 // let's pretend we're linux 3.2.0
after_note:
    .balign 4
.popsection
