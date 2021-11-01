# SPDX-License-Identifier: GPL-2.0
"""Test SNE algorithm implementations"""

import logging

import pytest

from .sne_alg import (
    SequenceNumberExtender,
    SequenceNumberExtenderLinux,
    SequenceNumberExtenderRFC,
)

logger = logging.getLogger(__name__)


# Data from https://datatracker.ietf.org/doc/draft-touch-sne/
_SNE_TEST_DATA = [
    (0x00000000, 0x00000000),
    (0x00000000, 0x30000000),
    (0x00000000, 0x90000000),
    (0x00000000, 0x70000000),
    (0x00000000, 0xA0000000),
    (0x00000001, 0x00000001),
    (0x00000000, 0xE0000000),
    (0x00000001, 0x00000000),
    (0x00000001, 0x7FFFFFFF),
    (0x00000001, 0x00000000),
    (0x00000001, 0x50000000),
    (0x00000001, 0x80000000),
    (0x00000001, 0x00000001),
    (0x00000001, 0x40000000),
    (0x00000001, 0x90000000),
    (0x00000001, 0xB0000000),
    (0x00000002, 0x0FFFFFFF),
    (0x00000002, 0x20000000),
    (0x00000002, 0x90000000),
    (0x00000002, 0x70000000),
    (0x00000002, 0xA0000000),
    (0x00000003, 0x00004000),
    (0x00000002, 0xD0000000),
    (0x00000003, 0x20000000),
    (0x00000003, 0x90000000),
    (0x00000003, 0x70000000),
    (0x00000003, 0xA0000000),
    (0x00000004, 0x00004000),
    (0x00000003, 0xD0000000),
]


# Easier test data with small jumps <= 0x30000000
SNE_DATA_EASY = [
    (0x00000000, 0x00000000),
    (0x00000000, 0x30000000),
    (0x00000000, 0x60000000),
    (0x00000000, 0x80000000),
    (0x00000000, 0x90000000),
    (0x00000000, 0xC0000000),
    (0x00000000, 0xF0000000),
    (0x00000001, 0x10000000),
    (0x00000000, 0xF0030000),
    (0x00000001, 0x00030000),
    (0x00000001, 0x10030000),
]


def check_sne_alg(alg, data):
    for sne, seq in data:
        observed_sne = alg.calc(seq)
        logger.info(
            "seq %08x expected sne %08x observed sne %08x", seq, sne, observed_sne
        )
        assert observed_sne == sne


def test_sne_alg():
    check_sne_alg(SequenceNumberExtender(), _SNE_TEST_DATA)


def test_sne_alg_easy():
    check_sne_alg(SequenceNumberExtender(), SNE_DATA_EASY)


@pytest.mark.xfail
def test_sne_alg_rfc():
    check_sne_alg(SequenceNumberExtenderRFC(), _SNE_TEST_DATA)


@pytest.mark.xfail
def test_sne_alg_rfc_easy():
    check_sne_alg(SequenceNumberExtenderRFC(), SNE_DATA_EASY)


def test_sne_alg_linux():
    check_sne_alg(SequenceNumberExtenderLinux(), _SNE_TEST_DATA)
    check_sne_alg(SequenceNumberExtenderLinux(), SNE_DATA_EASY)
