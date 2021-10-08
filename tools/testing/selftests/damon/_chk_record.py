#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

"Check whether the DAMON record file is valid"

import argparse
import struct
import sys

fmt_version = 0

def set_fmt_version(f):
    global fmt_version

    mark = f.read(16)
    if mark == b'damon_recfmt_ver':
        fmt_version = struct.unpack('i', f.read(4))[0]
    else:
        fmt_version = 0
        f.seek(0)
    return fmt_version

def read_pid(f):
    if fmt_version == 1:
        pid = struct.unpack('i', f.read(4))[0]
    else:
        pid = struct.unpack('L', f.read(8))[0]

def err_percent(val, expected):
    return abs(val - expected) / expected * 100

def chk_task_info(f):
    pid = read_pid(f)
    nr_regions = struct.unpack('I', f.read(4))[0]

    if nr_regions > max_nr_regions:
        print('too many regions: %d > %d' % (nr_regions, max_nr_regions))
        exit(1)

    nr_gaps = 0
    eaddr = 0
    for r in range(nr_regions):
        saddr = struct.unpack('L', f.read(8))[0]
        if eaddr and saddr != eaddr:
            nr_gaps += 1
        eaddr = struct.unpack('L', f.read(8))[0]
        nr_accesses = struct.unpack('I', f.read(4))[0]

        if saddr >= eaddr:
            print('wrong region [%d,%d)' % (saddr, eaddr))
            exit(1)

        max_nr_accesses = aint / sint
        if nr_accesses > max_nr_accesses:
            if err_percent(nr_accesses, max_nr_accesses) > 15:
                print('too high nr_access: expected %d but %d' %
                        (max_nr_accesses, nr_accesses))
                exit(1)
    if nr_gaps != 2:
        print('number of gaps are not two but %d' % nr_gaps)
        exit(1)

def parse_time_us(bindat):
    sec = struct.unpack('l', bindat[0:8])[0]
    nsec = struct.unpack('l', bindat[8:16])[0]
    return (sec * 1000000000 + nsec) / 1000

def main():
    global sint
    global aint
    global min_nr
    global max_nr_regions

    parser = argparse.ArgumentParser()
    parser.add_argument('file', metavar='<file>',
            help='path to the record file')
    parser.add_argument('--attrs', metavar='<attrs>',
            default='5000 100000 1000000 10 1000',
            help='content of debugfs attrs file')
    args = parser.parse_args()
    file_path = args.file
    attrs = [int(x) for x in args.attrs.split()]
    sint, aint, rint, min_nr, max_nr_regions = attrs

    with open(file_path, 'rb') as f:
        set_fmt_version(f)
        last_aggr_time = None
        while True:
            timebin = f.read(16)
            if len(timebin) != 16:
                break

            now = parse_time_us(timebin)
            if not last_aggr_time:
                last_aggr_time = now
            else:
                error = err_percent(now - last_aggr_time, aint)
                if error > 15:
                    print('wrong aggr interval: expected %d, but %d' %
                            (aint, now - last_aggr_time))
                    exit(1)
                last_aggr_time = now

            nr_tasks = struct.unpack('I', f.read(4))[0]
            for t in range(nr_tasks):
                chk_task_info(f)

if __name__ == '__main__':
    main()
