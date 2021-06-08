import pathlib
import sys

MOUNT = None
with open("/proc/mounts", 'r') as f:
    for line in f:
        if "sysfs" in line:
            MOUNT = line.split()[1]
            break

if MOUNT == None:
    print("ERROR: sysfs is not mounted")
    sys.exit(1)

SUNRPC = pathlib.Path(MOUNT) / "kernel" / "sunrpc"
if not SUNRPC.is_dir():
    print("ERROR: sysfs does not have sunrpc directory")
    sys.exit(1)
