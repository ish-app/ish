handle SIGUSR1 noprint pass
handle SIGTTIN noprint pass
set print thread-events off

define hook-run
    python
import subprocess
if subprocess.run('ninja').returncode != 0:
    raise gdb.CommandError('compilation failed')
    end
end
