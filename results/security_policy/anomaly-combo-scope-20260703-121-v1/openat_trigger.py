for _ in range(10):
    with open("/etc/hostname", "rb") as fp:
        fp.read(1)
