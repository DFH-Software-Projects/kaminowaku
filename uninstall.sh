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

# Reject unknown arguments BEFORE removing anything.
case "${1:-}" in
    ""|--keep-user-data|--purge-user-data) ;;
    *)
        fail "Unknown option '$1'. Use --keep-user-data or --purge-user-data."
        ;;
esac

[ "$(id -u)" -eq 0 ] || fail "Uninstall requires root privileges. Run with sudo or as root."

echo "[kaminowaku] removing installed binary"
rm -f "${BINDIR}/kaminowaku"

echo "[kaminowaku] removing installed runtime assets"
rm -rf "${SHARE_DIR}"

echo "[nosix] removing Kaminowaku-owned private NOSIX runtime"
rm -f "$LIBDIR/libnosix.so" "$LIBDIR/libnosix.so.1" "$LIBDIR/libnosix.so.1.4.0"
rmdir "$LIBDIR" 2>/dev/null || true
echo "[i] System NOSIX and OpenSSL installations have not been modified."

# Preserve projects, PCAP captures, books and logs unless expressly purged.
# Purge removes data for ALL local users; use only with explicit authorization.
case "${1:-}" in
    ""|--keep-user-data)
        echo "[kaminowaku] user projects, PCAP captures and logs preserved."
        echo "[i] To remove all users' Kaminowaku data, rerun with --purge-user-data."
        ;;
    --purge-user-data)
        echo "[x] Explicitly purging ALL local users' Kaminowaku projects, captures and logs."
        rm -rf /root/.kaminowaku
        rm -rf /home/*/.kaminowaku
        rm -rf /usr/home/*/.kaminowaku
        ;;
    *)
        fail "Unknown argument '$1'. Use --keep-user-data or --purge-user-data."
        ;;
esac

echo "Kaminowaku and its private NOSIX runtime have been uninstalled."
