# SPDX-License-Identifier: GPL-2.0
import socket
import struct
from contextlib import contextmanager
from enum import IntEnum

# Extra sockopts not present in python stdlib
TCP_REPAIR = 19
TCP_REPAIR_QUEUE = 20
TCP_QUEUE_SEQ = 21
TCP_REPAIR_OPTIONS = 22
TCP_REPAIR_WINDOW = 29


class TCP_REPAIR_VAL(IntEnum):
    OFF = 0
    ON = 1
    OFF_NO_WP = -1


def get_tcp_repair(sock) -> TCP_REPAIR_VAL:
    return TCP_REPAIR_VAL(sock.getsockopt(socket.SOL_TCP, TCP_REPAIR))


def set_tcp_repair(sock, val: TCP_REPAIR_VAL) -> None:
    return sock.setsockopt(socket.SOL_TCP, TCP_REPAIR, int(val))


class TCP_REPAIR_QUEUE_ID(IntEnum):
    NO_QUEUE = 0
    RECV_QUEUE = 1
    SEND_QUEUE = 2


def get_tcp_repair_queue(sock) -> TCP_REPAIR_QUEUE_ID:
    return TCP_REPAIR_QUEUE_ID(sock.getsockopt(socket.SOL_TCP, TCP_REPAIR_QUEUE))


def set_tcp_repair_queue(sock, val: TCP_REPAIR_QUEUE_ID) -> None:
    return sock.setsockopt(socket.SOL_TCP, TCP_REPAIR_QUEUE, int(val))


def get_tcp_queue_seq(sock) -> int:
    return struct.unpack("I", sock.getsockopt(socket.SOL_TCP, TCP_QUEUE_SEQ, 4))[0]


def set_tcp_queue_seq(sock, val: int) -> None:
    return sock.setsockopt(socket.SOL_TCP, TCP_QUEUE_SEQ, val)[0]


@contextmanager
def tcp_repair_toggle(sock, off_val=TCP_REPAIR_VAL.OFF_NO_WP):
    """Set TCP_REPAIR on/off as a context"""
    try:
        set_tcp_repair(sock, TCP_REPAIR_VAL.ON)
        yield
    finally:
        set_tcp_repair(sock, off_val)


def get_tcp_repair_recv_send_queue_seq(sock):
    with tcp_repair_toggle(sock):
        set_tcp_repair_queue(sock, TCP_REPAIR_QUEUE_ID.RECV_QUEUE)
        recv_seq = get_tcp_queue_seq(sock)
        set_tcp_repair_queue(sock, TCP_REPAIR_QUEUE_ID.SEND_QUEUE)
        send_seq = get_tcp_queue_seq(sock)
        return (recv_seq, send_seq)
