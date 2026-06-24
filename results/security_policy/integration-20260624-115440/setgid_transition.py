import errno
import os
import sys

target_gid = int(sys.argv[1]) if len(sys.argv) > 1 else 65534
try:
    os.setgid(target_gid)
except OSError as exc:
    print(os.strerror(exc.errno or errno.EPERM), file=sys.stderr)
    sys.exit(1)
sys.exit(0)
