import socket
import sysfs

class Xprt:
    def __init__(self, path):
        self.path = path
        self.id = int(str(path).rsplit("-", 2)[1])
        self.type = str(path).rsplit("-", 2)[2]
        self.dstaddr = open(path / "dstaddr", 'r').readline().strip()

        with open(path / "xprt_state") as f:
            self.state = ','.join(f.readline().split()[1:])
        self.__dict__.update(sysfs.read_info_file(path / "xprt_info"))

    def __lt__(self, rhs):
        return self.id < rhs.id

    def __str__(self):
        line = "xprt %s: %s, %s, state <%s>, num_reqs %s" % \
                (self.id, self.type, self.dstaddr, self.state, self.num_reqs)
        line += "\n	cur_cong %s, cong_win %s, min_num_slots %s, max_num_slots %s" % \
                (self.cur_cong, self.cong_win, self.min_num_slots, self.max_num_slots)
        line += "\n	binding_q_len %s, sending_q_len %s, pending_q_len %s, backlog_q_len %s" % \
                (self.binding_q_len, self.sending_q_len, self.pending_q_len, self.backlog_q_len)
        return line

    def small_str(self):
        return "xprt %s: %s, %s" % (self.id, self.type, self.dstaddr)

    def set_dstaddr(self, newaddr):
        resolved = socket.gethostbyname(newaddr)
        with open(self.path / "dstaddr", 'w') as f:
            f.write(resolved)
        self.dstaddr = open(self.path / "dstaddr", 'r').readline().strip()


def list_xprts(args):
    xprts = [ Xprt(f) for f in (sysfs.SUNRPC / "xprt-switches").glob("**/xprt-*") ]
    xprts.sort()
    for xprt in xprts:
        if args.id == None or xprt.id == args.id[0]:
            print(xprt)

def get_xprt(id):
    xprts = [ Xprt(f) for f in (sysfs.SUNRPC / "xprt-switches").glob("**/xprt-*") ]
    for xprt in xprts:
        if xprt.id == id:
            return xprt

def set_xprt_property(args):
    xprt = get_xprt(args.id[0])
    try:
        if args.dstaddr != None:
            xprt.set_dstaddr(args.dstaddr[0])
        print(xprt)
    except Exception as e:
        print(e)

def add_command(subparser):
    parser = subparser.add_parser("xprt", help="Commands for individual xprts")
    parser.add_argument("--id", metavar="ID", nargs=1, type=int, help="Id of a specific xprt to show")
    parser.set_defaults(func=list_xprts)

    subparser = parser.add_subparsers()
    parser = subparser.add_parser("set", help="Set an xprt property")
    parser.add_argument("--id", metavar="ID", nargs=1, type=int, required=True, help="Id of a specific xprt to modify")
    parser.add_argument("--dstaddr", metavar="dstaddr", nargs=1, type=str, help="New dstaddr to set")
    parser.set_defaults(func=set_xprt_property)
