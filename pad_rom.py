#!/usr/bin/env python
import sys

with open(sys.argv[1], "rb") as f:
    data = f.read()
    data = b'\0' * (0x7c00) + data + b'\0' * (0x10000 - 0x7c00 - 512)

assert len(data) == 0x10000

with open(sys.argv[2], "wb") as f:
    f.write(data)
