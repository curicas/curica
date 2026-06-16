import subprocess
import time

p = subprocess.Popen(['gdb', '--batch', '-ex', 'run', '-ex', 'info registers', '-ex', 'bt', '--args', './build/curica', 'run', './tests/test_gc_nursery.js'])
time.sleep(2)
p.send_signal(2) # SIGINT
p.wait()
