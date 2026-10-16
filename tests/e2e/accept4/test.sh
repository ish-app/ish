python3 -c "
import socket, ctypes, ctypes.util, threading, time

libc = ctypes.CDLL(ctypes.util.find_library('c'), use_errno=True)
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', 19999))
s.listen(1)

def connect():
    time.sleep(0.3)
    c = socket.socket()
    c.connect(('127.0.0.1', 19999))

threading.Thread(target=connect).start()

args = (ctypes.c_ulong * 4)(s.fileno(), 0, 0, 0)
result = libc.syscall(102, 18, args)
err = ctypes.get_errno()
print('accept4 ok' if result > 0 else 'accept4 failed errno=' + str(err))
"
