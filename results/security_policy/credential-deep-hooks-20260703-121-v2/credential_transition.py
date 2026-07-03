import os
import sys

target_uid = int(sys.argv[1])
os.setuid(target_uid)
print(f"uid={os.getuid()} euid={os.geteuid()}")
