#!/bin/sh
# Kaminowaku native offline-install smoke test (Linux or FreeBSD amd64).
# Tests the real installer under a throwaway PREFIX without modifying /usr/local.
set -eu

fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || fail "Run with sudo: sudo ./tests/offline-install-smoke.sh"

ROOT=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

case "$(uname -s)" in
    Linux) PLATFORM=linux ;;
    FreeBSD) PLATFORM=freebsd ;;
    *) fail "Only Linux and FreeBSD are supported." ;;
esac
case "$(uname -m)" in
    x86_64|amd64) ;;
    *) fail "Native smoke test requires amd64." ;;
esac
[ -s "release/$PLATFORM-amd64/bin/kaminowaku" ] || fail "Native release binary missing: release/$PLATFORM-amd64"
[ -s "release/$PLATFORM-amd64/BUILD-MANIFEST.txt" ] || fail "Native release manifest missing."

TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/kaminowaku-offline-install.XXXXXX") \
    || fail "Cannot allocate isolated test directory."
trap 'rm -rf "$TMP_ROOT"' EXIT
trap 'exit 1' HUP INT TERM

TEST_PREFIX="$TMP_ROOT/prefix"
GUARD_BIN="$TMP_ROOT/no-package-pulls"
FORBIDDEN_LOG="$TMP_ROOT/forbidden-commands.log"
INSTALL_LOG="$TMP_ROOT/install.log"
mkdir -p "$GUARD_BIN"

# PATH guards reject and log attempts to call package managers, downloaders,
# compilers or build tools. Only the prebuilt native install path may pass.
for name in apt apt-get aptitude pkg pkgconf pkg-config dnf yum zypper \
    curl wget git clang cc gcc make gmake; do
    cat > "$GUARD_BIN/$name" <<'BLOCKED'
#!/bin/sh
printf '%s\n' "${0##*/}" >> "$OFFLINE_FORBIDDEN_LOG"
printf 'ERROR: Forbidden dependency/package/build command: %s\n' "${0##*/}" >&2
exit 97
BLOCKED
    chmod 755 "$GUARD_BIN/$name"
done
: > "$FORBIDDEN_LOG"

printf '[test] Isolated %s/amd64 offline installation\n' "$PLATFORM"
printf '[test] PREFIX=%s\n' "$TEST_PREFIX"
if ! PATH="$GUARD_BIN:$PATH" OFFLINE_FORBIDDEN_LOG="$FORBIDDEN_LOG" \
    PREFIX="$TEST_PREFIX" ./install.sh install > "$INSTALL_LOG" 2>&1; then
    cat "$INSTALL_LOG" >&2
    fail "Offline prebuilt installation did not complete."
fi
if [ -s "$FORBIDDEN_LOG" ]; then
    cat "$INSTALL_LOG" >&2
    fail "Installer invoked a forbidden command: $(cat "$FORBIDDEN_LOG")"
fi
grep -F '[install] Using verified prebuilt native release' "$INSTALL_LOG" >/dev/null \
    || { cat "$INSTALL_LOG" >&2; fail "Installer silently rebuilt instead of using the native binary."; }

BIN="$TEST_PREFIX/bin/kaminowaku"
LIBDIR="$TEST_PREFIX/lib/kaminowaku"
[ -x "$BIN" ] || fail "Installed executable missing."
[ -s "$LIBDIR/libnosix.so.1.4.0" ] || fail "Private NOSIX shared library missing."
[ -L "$LIBDIR/libnosix.so.1" ] || fail "Private NOSIX SONAME symlink missing."
[ -s "$TEST_PREFIX/share/kaminowaku/profiles/default.ini" ] || fail "Default profile missing."
[ -s "$TEST_PREFIX/share/kaminowaku/licenses/OPENSSL-LICENSE.txt" ] || fail "OpenSSL license missing."
[ -s "$TEST_PREFIX/share/kaminowaku/licenses/NOSIX-LICENSE.txt" ] || fail "NOSIX license missing."
[ -s "$TEST_PREFIX/share/kaminowaku/books/ssh-kex.lua" ] || fail "Packaged Books missing."

if command -v ldd >/dev/null 2>&1; then
    ldd "$BIN" > "$TMP_ROOT/ldd.txt" 2>&1 || {
        cat "$TMP_ROOT/ldd.txt" >&2
        fail "Installed executable has unresolved dependencies."
    }
    grep -F 'libnosix.so.1' "$TMP_ROOT/ldd.txt" >/dev/null \
        || fail "Installed executable does not depend on the expected NOSIX SONAME."
    command -v realpath >/dev/null 2>&1 || fail "realpath is required for NOSIX path verification."
    LOADED_NOSIX=$(awk '$1 == "libnosix.so.1" && $2 == "=>" { print $3; exit }' "$TMP_ROOT/ldd.txt")
    case "$LOADED_NOSIX" in
        /*) ;;
        *) cat "$TMP_ROOT/ldd.txt" >&2
           fail "Cannot determine an absolute NOSIX loader path from ldd." ;;
    esac
    EXPECTED_NOSIX=$(realpath "$LIBDIR/libnosix.so.1.4.0") \
        || fail "Cannot resolve installed private NOSIX library."
    ACTUAL_NOSIX=$(realpath "$LOADED_NOSIX") || {
        cat "$TMP_ROOT/ldd.txt" >&2
        fail "Cannot resolve NOSIX loader path: $LOADED_NOSIX"
    }
    if [ "$EXPECTED_NOSIX" != "$ACTUAL_NOSIX" ]; then
        printf 'Expected private NOSIX: %s\nActual loaded NOSIX:   %s\n' \
            "$EXPECTED_NOSIX" "$ACTUAL_NOSIX" >&2
        cat "$TMP_ROOT/ldd.txt" >&2
        fail "NOSIX did not load from its private installation directory."
    fi
    if grep -E 'libssl[.]so|libcrypto[.]so|not found' "$TMP_ROOT/ldd.txt" >/dev/null; then
        cat "$TMP_ROOT/ldd.txt" >&2
        fail "Shared OpenSSL linkage or missing runtime dependency detected."
    fi
fi

printf '[+] PASS: prebuilt install, private NOSIX, static OpenSSL, runtime assets\n'
printf '[+] PASS: no compiler, build tool, package manager, or downloader invoked\n'
printf '[i] Test prefix cleaned automatically; existing /usr/local install was untouched.\n'
