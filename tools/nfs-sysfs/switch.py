import sysfs
import xprt

class XprtSwitch:
    def __init__(self, path, sep=":"):
        self.path = path
        self.id = int(str(path).rsplit("-", 1)[1])
        self.sep = sep

        self.xprts = [ xprt.Xprt(p) for p in self.path.iterdir() if p.is_dir() ]
        self.xprts.sort()

        self.__dict__.update(sysfs.read_info_file(path / "xprt_switch_info"))

    def __lt__(self, rhs):
        return self.path < rhs.path

    def __str__(self):
        line = "switch %s%s num_xprts %s, num_active %s, queue_len %s" % \
                (self.id, self.sep, self.num_xprts, self.num_active, self.queue_len)
        for x in self.xprts:
            line += "\n	%s" % x.small_str()
        return line


def list_xprt_switches(args):
    switches = [ XprtSwitch(f) for f in (sysfs.SUNRPC / "xprt-switches").iterdir() ]
    switches.sort()
    for xs in switches:
        if args.id == None or xs.id == args.id[0]:
            print(xs)

def set_xprt_switch_property(args):
    switch = XprtSwitch(sysfs.SUNRPC / "xprt-switches" / f"switch-{args.id[0]}")
    try:
        for xprt in switch.xprts:
            xprt.set_dstaddr(args.dstaddr[0])
        print(switch)
    except Exception as e:
        print(e)

def add_command(subparser):
    parser = subparser.add_parser("xprt-switch", help="Commands for xprt switches")
    parser.add_argument("--id", metavar="ID", nargs=1, type=int, help="Id of a specific xprt-switch to show")
    parser.set_defaults(func=list_xprt_switches)

    subparser = parser.add_subparsers()
    parser = subparser.add_parser("set", help="Set an xprt switch property")
    parser.add_argument("--id", metavar="ID", nargs=1, type=int, required=True, help="Id of an xprt-switch to modify")
    parser.add_argument("--dstaddr", metavar="dstaddr", nargs=1, type=str, help="New dstaddr to set")
    parser.set_defaults(func=set_xprt_switch_property)
