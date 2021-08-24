# SPDX-License-Identifier: GPL-2.0
import logging
import os
import selectors
from contextlib import ExitStack
from threading import Thread

logger = logging.getLogger(__name__)


class SimpleServerThread(Thread):
    """Simple server thread for testing TCP sockets

    All data is read in 1000 bytes chunks and either echoed back or discarded.
    """

    def __init__(self, socket, mode="recv"):
        self.listen_socket = socket
        self.server_socket = []
        self.mode = mode
        super().__init__()

    def _read(self, conn, events):
        # logger.debug("events=%r", events)
        data = conn.recv(1000)
        # logger.debug("len(data)=%r", len(data))
        if len(data) == 0:
            # logger.info("closing %r", conn)
            conn.close()
            self.sel.unregister(conn)
        else:
            if self.mode == "echo":
                conn.sendall(data)
            elif self.mode == "recv":
                pass
            else:
                raise ValueError(f"Unknown mode {self.mode}")

    def _stop_pipe_read(self, conn, events):
        self.should_loop = False

    def start(self) -> None:
        self.exit_stack = ExitStack()
        self._stop_pipe_rfd, self._stop_pipe_wfd = os.pipe()
        self.exit_stack.callback(lambda: os.close(self._stop_pipe_rfd))
        self.exit_stack.callback(lambda: os.close(self._stop_pipe_wfd))
        return super().start()

    def _accept(self, conn, events):
        assert conn == self.listen_socket
        conn, _addr = self.listen_socket.accept()
        conn = self.exit_stack.enter_context(conn)
        conn.setblocking(False)
        self.sel.register(conn, selectors.EVENT_READ, self._read)
        self.server_socket.append(conn)

    def run(self):
        self.should_loop = True
        self.sel = self.exit_stack.enter_context(selectors.DefaultSelector())
        self.sel.register(
            self._stop_pipe_rfd, selectors.EVENT_READ, self._stop_pipe_read
        )
        self.sel.register(self.listen_socket, selectors.EVENT_READ, self._accept)
        # logger.debug("loop init")
        while self.should_loop:
            for key, events in self.sel.select(timeout=1):
                callback = key.data
                callback(key.fileobj, events)
        # logger.debug("loop done")

    def stop(self):
        """Try to stop nicely"""
        os.write(self._stop_pipe_wfd, b"Q")
        self.join()
        self.exit_stack.close()

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *args):
        self.stop()
