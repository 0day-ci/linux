import sysfs
import switch

class RpcClient:
    def __init__(self, path):
        self.path = path
        self.id = int(str(path).rsplit("-", 1)[1])
        self.switch = switch.XprtSwitch(path / (path / "switch").readlink(), sep=",")

    def __lt__(self, rhs):
        return self.id < rhs.id

    def __str__(self):
        return "client %s: %s" % (self.id, self.switch)


def list_rpc_clients(args):
    clients = [ RpcClient(f) for f in (sysfs.SUNRPC / "rpc-clients").iterdir() ]
    clients.sort()
    for client in clients:
        if args.id == None or client.id == args.id[0]:
            print(client)

def add_command(subparser):
    parser = subparser.add_parser("rpc-client", help="Commands for rpc clients")
    parser.add_argument("--id", metavar="ID", nargs=1, type=int, help="Id of a specific client to show")
    parser.set_defaults(func=list_rpc_clients)
