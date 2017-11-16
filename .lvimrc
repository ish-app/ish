let g:neomake_c_enabled_makers = ['clangcheck']
let g:neomake_c_clangcheck_maker = {
    \ 'exe': 'clang-check',
    \ 'args': ['-p', 'build', '-extra-arg=-Xclang', '-extra-arg=-fno-color-diagnostics', '%:p'],
    \ 'errorformat':
        \ '%-G../%f:%s:,' .
        \ '../%f:%l:%c: %trror: %m,' .
        \ '../%f:%l:%c: %tarning: %m,' .
        \ '%I../%f:%l:%c: note: %m,' .
        \ '../%f:%l:%c: %m,'.
        \ '../%f:%l: %trror: %m,'.
        \ '../%f:%l: %tarning: %m,'.
        \ '%I../%f:%l: note: %m,'.
        \ '../%f:%l: %m',
\ }
