#!/bin/sh
# Copyright 2026 Jamison A. Drapeau

set -eu

PREFIX="${PREFIX:-/usr/local}"
BINDIR="${PREFIX}/bin"
INCLUDEDIR="${PREFIX}/include"
LIBDIR="${PREFIX}/lib/kaminowaku"
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

echo "[nosix] removing Kaminowaku-owned private NOSIX runtime"
rm -f "$LIBDIR/libnosix.so" "$LIBDIR/libnosix.so.1" "$LIBDIR/libnosix.so.1.4.0"
rmdir "$LIBDIR" 2>/dev/null || true
echo "[i] System NOSIX and OpenSSL installations have not been modified."

echo "[kaminowaku] removing user data"
rm -rf /root/.kaminowaku
rm -rf /home/*/.kaminowaku
rm -rf /usr/home/*/.kaminowaku

echo "Kaminowaku and its private NOSIX runtime have been uninstalled."
