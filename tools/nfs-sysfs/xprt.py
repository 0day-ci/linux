class Xprt:
    def __init__(self, path):
        self.path = path
        self.id = int(str(path).rsplit("-", 2)[1])
        self.type = str(path).rsplit("-", 2)[2]
        self.dstaddr = open(path / "dstaddr", 'r').readline().strip()

    def __lt__(self, rhs):
        return self.id < rhs.id

    def small_str(self):
        return "xprt %s: %s, %s" % (self.id, self.type, self.dstaddr)
