#!/usr/bin/python
import argparse
import sysfs

parser = argparse.ArgumentParser()

def show_small_help(args):
    parser.print_usage()
    print("sunrpc dir:", sysfs.SUNRPC)
parser.set_defaults(func=show_small_help)


import switch
subparser = parser.add_subparsers(title="commands")
switch.add_command(subparser)


args = parser.parse_args()
args.func(args)
