# SPDX-License-Identifier: GPL-2.0
"""Python implementation of SNE algorithms"""


def distance(x, y):
    if x < y:
        return y - x
    else:
        return x - y


class SequenceNumberExtender:
    """Based on https://datatracker.ietf.org/doc/draft-touch-sne/"""

    sne: int = 0
    sne_flag: int = 1
    prev_seq: int = 0

    def calc(self, seq):
        """Update internal state and return SNE for certain SEQ"""
        # use current SNE to start
        result = self.sne

        # both in same SNE range?
        if distance(seq, self.prev_seq) < 0x80000000:
            # jumps fwd over N/2?
            if seq >= 0x80000000 and self.prev_seq < 0x80000000:
                self.sne_flag = 0
            # move prev forward if needed
            self.prev_seq = max(seq, self.prev_seq)
        # both in diff SNE ranges?
        else:
            # jumps forward over zero?
            if seq < 0x80000000:
                # update prev
                self.prev_seq = seq
                # first jump over zero? (wrap)
                if self.sne_flag == 0:
                    # set flag so we increment once
                    self.sne_flag = 1
                    # increment window
                    self.sne = self.sne + 1
                    # use updated SNE value
                    result = self.sne
            # jump backward over zero?
            else:
                # use pre-rollover SNE value
                result = self.sne - 1

        return result


class SequenceNumberExtenderRFC:
    """Based on sample code in original RFC5925 document"""

    sne: int = 0
    sne_flag: int = 1
    prev_seq: int = 0

    def calc(self, seq):
        """Update internal state and return SNE for certain SEQ"""
        # set the flag when the SEG.SEQ first rolls over
        if self.sne_flag == 0 and self.prev_seq > 0x7FFFFFFF and seq < 0x7FFFFFFF:
            self.sne = self.sne + 1
            self.sne_flag = 1
        # decide which SNE to use after incremented
        if self.sne_flag and seq > 0x7FFFFFFF:
            # use the pre-increment value
            sne = self.sne - 1
        else:
            # use the current value
            sne = self.sne
        # reset the flag in the *middle* of the window
        if self.prev_seq < 0x7FFFFFFF and seq > 0x7FFFFFFF:
            self.sne_flag = 0
        # save the current SEQ for the next time through the code
        self.prev_seq = seq

        return sne


def tcp_seq_before(a, b) -> bool:
    return ((a - b) & 0xFFFFFFFF) > 0x80000000


def tcp_seq_after(a, b) -> bool:
    return tcp_seq_before(a, b)


class SequenceNumberExtenderLinux:
    """Based on linux implementation and with no extra flags"""

    sne: int = 0
    prev_seq: int = 0

    def reset(self, seq, sne=0):
        self.prev_seq = seq
        self.sne = sne

    def calc(self, seq, update=True):
        sne = self.sne
        if tcp_seq_before(seq, self.prev_seq):
            if seq > self.prev_seq:
                sne -= 1
        else:
            if seq < self.prev_seq:
                sne += 1
        if update and tcp_seq_before(self.prev_seq, seq):
            self.prev_seq = seq
            self.sne = sne
        return sne
