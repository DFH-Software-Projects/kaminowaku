#!/bin/sh
# Copyright 2026 Jamison A. Drapeau

set -eu

PREFIX="${PREFIX:-/usr/local}"
BINDIR="${PREFIX}/bin"
INCLUDEDIR="${PREFIX}/include"
LIBDIR="${PREFIX}/lib"
SHARE_DIR="${PREFIX}/share/kaminowaku"

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

[ "$(id -u)" -eq 0 ] || fail "Uninstall requires root privileges. Run with sudo or as root."

echo "[kaminowaku] removing installed binary"
rm -f "${BINDIR}/kaminowaku"

echo "[kaminowaku] removing installed runtime assets"
rm -rf "${SHARE_DIR}"

echo "[nosix] removing installed public headers"
rm -f \
    "${INCLUDEDIR}/nosix.h" \
    "${INCLUDEDIR}/nosix_poll.h" \
    "${INCLUDEDIR}/nosix_datagram.h"

echo "[nosix] removing installed shared libraries"
rm -f "${LIBDIR}"/libnosix.so "${LIBDIR}"/libnosix.so.*

echo "[nosix] refreshing shared-library loader state"
case "$(uname -s)" in
    Linux)
        if command -v ldconfig >/dev/null 2>&1; then
            ldconfig
        fi
        ;;
    FreeBSD)
        if command -v ldconfig >/dev/null 2>&1; then
            ldconfig -m "${LIBDIR}" >/dev/null 2>&1 || true
        fi
        ;;
esac

echo "[kaminowaku] removing user data"
rm -rf /root/.kaminowaku
rm -rf /home/*/.kaminowaku
rm -rf /usr/home/*/.kaminowaku

echo "Kaminowaku and the packaged NOSIX ABI have been uninstalled."
