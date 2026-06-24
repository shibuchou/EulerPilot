import ctypes
import errno
import os
import sys

libc = ctypes.CDLL(None, use_errno=True)
rc = libc.ptrace(0, 0, None, None)
if rc != 0:
    err = ctypes.get_errno()
    print(os.strerror(err or errno.EPERM), file=sys.stderr)
    sys.exit(1)
