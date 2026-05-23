#!/bin/sh
# Entrypoint for the xpmile-scripts-test image. Installs the Python test
# deps (cached in /root/.cache/pip via a podman volume mounted by the
# caller) and exec's pytest with whatever args were passed.

set -e

pip install \
    --quiet --disable-pip-version-check \
    'pymongo>=4,<5' \
    'mongomock>=4,<5' \
    'pytest>=7,<9'

exec pytest "$@"
