#! /bin/bash

if ! command -v tox >/dev/null; then
	echo >&2 "error: please install the python tox package"
	exit 1
fi
if [[ $(id -u) -ne 0 ]]; then
	echo >&2 "warning: running as non-root user is unlikely to work"
fi
cd "$(dirname "${BASH_SOURCE[0]}")"
exec tox "$@"
