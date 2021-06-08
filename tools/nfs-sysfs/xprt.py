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


def list_xprts(args):
    xprts = [ Xprt(f) for f in (sysfs.SUNRPC / "xprt-switches").glob("**/xprt-*") ]
    xprts.sort()
    for xprt in xprts:
        if args.id == None or xprt.id == args.id[0]:
            print(xprt)

def add_command(subparser):
    parser = subparser.add_parser("xprt", help="Commands for individual xprts")
    parser.add_argument("--id", metavar="ID", nargs=1, type=int, help="Id of a specific xprt to show")
    parser.set_defaults(func=list_xprts)
