import sysfs
import xprt

class XprtSwitch:
    def __init__(self, path):
        self.path = path
        self.id = int(str(path).rsplit("-", 1)[1])

        self.xprts = [ xprt.Xprt(p) for p in self.path.iterdir() if p.is_dir() ]
        self.xprts.sort()

        self.__dict__.update(sysfs.read_info_file(path / "xprt_switch_info"))

    def __lt__(self, rhs):
        return self.path < rhs.path

    def __str__(self):
        line = "switch %s: num_xprts %s, num_active %s, queue_len %s" % \
                (self.id, self.num_xprts, self.num_active, self.queue_len)
        for x in self.xprts:
            line += "\n	%s" % x.small_str()
        return line


def list_xprt_switches(args):
    switches = [ XprtSwitch(f) for f in (sysfs.SUNRPC / "xprt-switches").iterdir() ]
    switches.sort()
    for xs in switches:
        if args.id == None or xs.id == args.id[0]:
            print(xs)

def add_command(subparser):
    parser = subparser.add_parser("xprt-switch", help="Commands for xprt switches")
    parser.add_argument("--id", metavar="ID", nargs=1, type=int, help="Id of a specific xprt-switch to show")
    parser.set_defaults(func=list_xprt_switches)
