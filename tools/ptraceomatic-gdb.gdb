source ish-gdb.gdb

define diff-mem
    dump binary memory real.bin real_page real_page+4096
    dump binary memory fake.bin fake_page fake_page+4096
    shell xxd real.bin > real.xxd
    shell xxd fake.bin > fake.xxd
    shell nvim -d real.xxd fake.xxd
end
