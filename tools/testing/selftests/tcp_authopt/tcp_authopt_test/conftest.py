# SPDX-License-Identifier: GPL-2.0
from tcp_authopt_test.linux_tcp_authopt import has_tcp_authopt
import pytest
import logging
from contextlib import ExitStack

logger = logging.getLogger(__name__)

skipif_missing_tcp_authopt = pytest.mark.skipif(
    not has_tcp_authopt(), reason="Need CONFIG_TCP_AUTHOPT"
)


@pytest.fixture
def exit_stack():
    """Return a contextlib.ExitStack as a pytest fixture

    This reduces indentation making code more readable
    """
    with ExitStack() as exit_stack:
        yield exit_stack
