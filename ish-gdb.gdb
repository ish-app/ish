handle SIGUSR1 noprint pass
handle SIGTTIN noprint pass
handle SIGPIPE noprint pass
set print thread-events off

define hook-run
    python
import subprocess
if subprocess.call('ninja') != 0:
    raise gdb.CommandError('compilation failed')
    end
end

define hook-stop
    python
try:
    symtab = gdb.selected_frame().find_sal().symtab
except:
    pass
else:
    if symtab is not None and symtab.filename.endswith('.S'):
        gdb.execute('set disassemble-next-line on')
    else:
        gdb.execute('set disassemble-next-line auto')
    end
end
