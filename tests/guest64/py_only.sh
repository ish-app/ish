python3 -c '
import sys, hashlib, json, math, struct, threading, subprocess, os
print("python", sys.version.split()[0], "maxsize", hex(sys.maxsize), "ptr bytes", struct.calcsize("P"))
print("2**100 =", 2**100, " pi*e =", round(math.pi*math.e, 6))
print("sha256:", hashlib.sha256(b"ish64").hexdigest()[:16])
print("json:", json.dumps({"a": [1, 2.5, None]}))
out = []
ts = [threading.Thread(target=lambda i=i: out.append(i*i)) for i in range(8)]
[t.start() for t in ts]; [t.join() for t in ts]
print("threads:", sorted(out))
print("subprocess:", subprocess.run(["uname", "-m"], capture_output=True, text=True).stdout.strip())
print("big list sum:", sum(range(1000000)))
'
