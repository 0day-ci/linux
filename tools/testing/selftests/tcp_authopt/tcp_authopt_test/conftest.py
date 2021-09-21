# SPDX-License-Identifier: GPL-2.0
import logging
import os
from contextlib import ExitStack

import pytest

from .linux_tcp_authopt import has_tcp_authopt, enable_sysctl_tcp_authopt

logger = logging.getLogger(__name__)

skipif_missing_tcp_authopt = pytest.mark.skipif(
    not has_tcp_authopt(), reason="Need CONFIG_TCP_AUTHOPT"
)


def can_capture():
    # This is too restrictive:
    return os.geteuid() == 0


skipif_cant_capture = pytest.mark.skipif(
    not can_capture(), reason="run as root to capture packets"
)


@pytest.fixture
def exit_stack():
    """Return a contextlib.ExitStack as a pytest fixture

    This reduces indentation making code more readable
    """
    with ExitStack() as exit_stack:
        yield exit_stack


def pytest_configure():
    # Silence messages regarding netns enter/exit:
    logging.getLogger("nsenter").setLevel(logging.INFO)
    if has_tcp_authopt():
        enable_sysctl_tcp_authopt()
