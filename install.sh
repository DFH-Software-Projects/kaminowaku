#!/bin/sh
# Copyright 2026 Jamison A. Drapeau

set -eu

TARGET="install"
BUILD="release"

PREFIX="${PREFIX:-/usr/local}"
BINDIR="${PREFIX}/bin"
INCLUDEDIR="${PREFIX}/include"
LIBDIR="${PREFIX}/lib"
SHARE_DIR="${PREFIX}/share/kaminowaku"
SHARE_PROFILES_DIR="${SHARE_DIR}/profiles"
SHARE_TOOLS_DIR="${SHARE_DIR}/tools"
SHARE_BOOKS_DIR="${SHARE_DIR}/books"
SHARE_BOOKS_MAIN_DIR="${SHARE_BOOKS_DIR}/main"
SHARE_BOOKS_MODULES_DIR="${SHARE_BOOKS_DIR}/modules"
SHARE_LICENSES_DIR="${SHARE_DIR}/licenses"
LEGACY_SHARE_LUA_DIR="${SHARE_DIR}/lua"
DEFAULT_PROFILE_SRC="./default.ini"
DEFAULT_PROFILE_DST="${SHARE_PROFILES_DIR}/default.ini"
BOOKS_SRC_DIR="./books"
SOURCE_ROOT="./src"
STAGE_ROOT="./.STAGE"
SYSTEM_BOOK_SSH_KEX="./books/ssh-kex.lua"
SYSTEM_BOOK_WEB_ENUM="./books/web-enum.lua"
MAIN_TRANSPORT="./books/main/transport.lua"
MAIN_SEND="./books/main/send.lua"
MAIN_RECV="./books/main/recv.lua"
MAIN_BYTES="./books/main/bytes.lua"
MAIN_TLS="./books/main/tls.lua"
MAIN_RESULT="./books/main/result.lua"
MODULE_SSH="./books/modules/ssh.lua"
MODULE_HTTP="./books/modules/http.lua"
OPENSSL_VERSION="3.5.8"
OPENSSL_SHA256="a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2"
OPENSSL_ROOT="./libs/openssl"
OPENSSL_ARCHIVE="$OPENSSL_ROOT/openssl-$OPENSSL_VERSION.tar.gz"
OPENSSL_LICENSE="$OPENSSL_ROOT/LICENSE.txt"
OPENSSL_INCLUDEDIR=""
OPENSSL_LIBDIR=""
OPENSSL_ABI_ENV=""
OPENSSL_EXTRA_LIBS=""
PRIVATE_LIBDIR="$LIBDIR/kaminowaku"
PACKAGED_NOSIX_ROOT="./libs/nosix"
PACKAGED_NOSIX_LICENSE="${PACKAGED_NOSIX_ROOT}/LICENSE"
PACKAGED_NOSIX_MANIFEST="${PACKAGED_NOSIX_ROOT}/BUILD-MANIFEST.txt"
PACKAGED_NOSIX=0
PLATFORM_TAG=""
ARCH_TAG=""
NOSIX_INCLUDEDIR="$INCLUDEDIR"
NOSIX_LIBDIR="$STAGE_ROOT/deps/nosix/lib"
NOSIX_SOURCE_LIBDIR=""
NOSIX_ABI_ENV=""
NOSIX_LINKER_NAME=""
NOSIX_SONAME_NAME=""
NOSIX_REAL_NAME=""

usage() {
    echo "Kaminowaku Install Script"
    echo ""
    echo "Usage:"
    echo "  ./install.sh [target] [BUILD=debug|release]"
    echo ""
    echo "Targets:"
    echo "  check     Validate compiler, bundled OpenSSL, packaged NOSIX ABI, and assets"
    echo "  install   Build/install binary, private NOSIX ABI, licenses, and assets (default)"
    echo "  all       Clean and build only"
    echo "  clean     Remove build artifacts"
    echo "  info      Show build/install configuration"
    echo ""
    echo "Install paths:"
    echo "  binary:           ${BINDIR}/kaminowaku"
    echo "  share root:       ${SHARE_DIR}"
    echo "  default profile:  ${DEFAULT_PROFILE_DST}"
    echo "  system books:     ${SHARE_BOOKS_DIR}"
    echo "  core Lua API:     ${SHARE_BOOKS_MAIN_DIR}"
    echo "  Book modules:     ${SHARE_BOOKS_MODULES_DIR}"
    echo ""
    echo "Examples:"
    echo "  ./install.sh check"
    echo "  ./install.sh"
    echo "  ./install.sh install BUILD=release"
    echo "  ./install.sh install BUILD=debug"
    echo "  ./install.sh all BUILD=release"
}

fail() {
    echo "ERROR: $*" >&2
    exit 1
}

if [ "$#" -gt 0 ]; then
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        check|install|all|clean|info)
            TARGET="$1"
            shift
            ;;
        BUILD=debug|BUILD=release)
            ;;
        *)
            fail "Unsupported target or argument '$1'. Use --help for usage."
            ;;
    esac
fi

for arg in "$@"; do
    case "$arg" in
        BUILD=debug)
            BUILD="debug"
            ;;
        BUILD=release)
            BUILD="release"
            ;;
        *)
            fail "Unsupported argument '$arg'. Use BUILD=debug or BUILD=release."
            ;;
    esac
done

command -v make >/dev/null 2>&1 || fail "make not found."

UNAME_S=$(uname -s)
UNAME_M=$(uname -m)

# No package-manager access is performed on installation targets.
sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v sha256 >/dev/null 2>&1; then
        sha256 -q "$1"
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        fail "SHA256 tool missing (sha256sum, sha256, or shasum)."
    fi
}

check_packaged_openssl() {
    [ -f "$OPENSSL_ARCHIVE" ] || fail "Release missing bundled OpenSSL source: $OPENSSL_ARCHIVE"
    [ -f "$OPENSSL_LICENSE" ] || fail "Release missing original OpenSSL license: $OPENSSL_LICENSE"
    EXPECTED_LINE=$(awk 'NR==1 {print $1}' "$OPENSSL_ROOT/SHA256")
    [ "$EXPECTED_LINE" = "$OPENSSL_SHA256" ] || fail "Unexpected OpenSSL checksum in release."
    [ "$(sha256_file "$OPENSSL_ARCHIVE")" = "$OPENSSL_SHA256" ] \
        || fail "Bundled OpenSSL source checksum mismatch."
    [ -f "$OPENSSL_ABI_ENV" ] || fail "Missing packaged OpenSSL ABI metadata for $PLATFORM_TAG."
    [ -f "$OPENSSL_INCLUDEDIR/openssl/ssl.h" ] || fail "Missing packaged OpenSSL headers."
    [ -f "$OPENSSL_LIBDIR/libssl.a" ] || fail "Missing packaged OpenSSL libssl.a."
    [ -f "$OPENSSL_LIBDIR/libcrypto.a" ] || fail "Missing packaged OpenSSL libcrypto.a."
    grep -qx "OPENSSL_VERSION=$OPENSSL_VERSION" "$OPENSSL_ABI_ENV" || fail "Wrong OpenSSL version."
    grep -qx "PLATFORM=$PLATFORM_TAG" "$OPENSSL_ABI_ENV" || fail "Wrong OpenSSL platform."
    grep -qx "ARCH=$ARCH_TAG" "$OPENSSL_ABI_ENV" || fail "Wrong OpenSSL architecture."
    grep -qx "SHA256=$OPENSSL_SHA256" "$OPENSSL_ABI_ENV" || fail "Wrong OpenSSL source checksum."
    SSL_EXPECTED=$(sed -n 's/^LIBSSL_SHA256=//p' "$OPENSSL_ABI_ENV")
    CRYPTO_EXPECTED=$(sed -n 's/^LIBCRYPTO_SHA256=//p' "$OPENSSL_ABI_ENV")
    [ -n "$SSL_EXPECTED" ] && [ -n "$CRYPTO_EXPECTED" ] || fail "Missing OpenSSL binary digests."
    [ "$(sha256_file "$OPENSSL_LIBDIR/libssl.a")" = "$SSL_EXPECTED" ] \
        || fail "libssl.a digest mismatch."
    [ "$(sha256_file "$OPENSSL_LIBDIR/libcrypto.a")" = "$CRYPTO_EXPECTED" ] \
        || fail "libcrypto.a digest mismatch."
    echo "[openssl] packaged $OPENSSL_VERSION verified for $PLATFORM_TAG/$ARCH_TAG"
}

# Clean and info require neither runtime libraries nor a compiler.
if [ "$TARGET" = "check" ] || [ "$TARGET" = "install" ] || [ "$TARGET" = "all" ]; then
    command -v clang >/dev/null 2>&1 || fail "clang not found."
    CC_VERSION=$(clang --version 2>/dev/null || true)
    echo "$CC_VERSION" | grep -qi "clang" || fail "Invalid clang compiler."
fi

[ -f ./Makefile ] || fail "Makefile not found in $(pwd)."

CPPFLAGS=""
case "$UNAME_S" in
    Linux)
        PLATFORM_TAG="linux"
        CPPFLAGS="-DKMN_OS_LINUX"
        ;;
    FreeBSD)
        PLATFORM_TAG="freebsd"
        CPPFLAGS="-DKMN_OS_FREEBSD"
        ;;
    *)
        PLATFORM_TAG="unknown"
        CPPFLAGS="-DKMN_OS_UNKNOWN"
        ;;
esac

case "$UNAME_M" in
    x86_64|amd64)
        ARCH_TAG="amd64"
        CPPFLAGS="$CPPFLAGS -DKMN_ARCH_X86_64"
        ;;
    aarch64|arm64)
        ARCH_TAG="arm64"
        CPPFLAGS="$CPPFLAGS -DKMN_ARCH_AARCH64"
        ;;
    *)
        ARCH_TAG="unknown"
        CPPFLAGS="$CPPFLAGS -DKMN_ARCH_UNKNOWN"
        ;;
esac

case "$TARGET" in
    check|install|all)
        [ -d "$PACKAGED_NOSIX_ROOT" ] \
            || fail "Release payload is missing the shipped NOSIX ABI: $PACKAGED_NOSIX_ROOT"
        ;;
esac

if [ -d "$PACKAGED_NOSIX_ROOT" ]; then
    [ "$PLATFORM_TAG" != "unknown" ] || fail "Packaged NOSIX ABI is unsupported on $UNAME_S."
    [ "$ARCH_TAG" != "unknown" ] || fail "Packaged NOSIX ABI is unsupported on $UNAME_M."
    PACKAGED_NOSIX=1
    NOSIX_INCLUDEDIR="$PACKAGED_NOSIX_ROOT/include"
    NOSIX_SOURCE_LIBDIR="$PACKAGED_NOSIX_ROOT/$PLATFORM_TAG/lib"
    NOSIX_ABI_ENV="$PACKAGED_NOSIX_ROOT/$PLATFORM_TAG/abi.env"
fi

OPENSSL_INCLUDEDIR="$OPENSSL_ROOT/$PLATFORM_TAG/include"
OPENSSL_LIBDIR="$OPENSSL_ROOT/$PLATFORM_TAG/lib"
OPENSSL_ABI_ENV="$OPENSSL_ROOT/$PLATFORM_TAG/abi.env"
if [ "$PLATFORM_TAG" = "linux" ]; then
    OPENSSL_EXTRA_LIBS="-ldl"
fi

validate_abi_name() {
    NAME="$1"
    LABEL="$2"

    [ -n "$NAME" ] || fail "Packaged NOSIX $LABEL is empty."
    case "$NAME" in
        *[!A-Za-z0-9._-]*|*/*)
            fail "Packaged NOSIX $LABEL contains an unsafe filename: $NAME"
            ;;
    esac
}

prepare_packaged_nosix_abi() {
    [ "$PACKAGED_NOSIX" -eq 1 ] || return 0

    [ -f "$NOSIX_ABI_ENV" ] || fail "Packaged NOSIX ABI metadata missing: $NOSIX_ABI_ENV"
    [ -f "$PACKAGED_NOSIX_LICENSE" ] || fail "Packaged NOSIX license missing: $PACKAGED_NOSIX_LICENSE"
    [ -f "$PACKAGED_NOSIX_MANIFEST" ] || fail "Packaged NOSIX build manifest missing: $PACKAGED_NOSIX_MANIFEST"

    PLATFORM=""
    ARCH=""
    LINKER_NAME=""
    SONAME_NAME=""
    REAL_NAME=""

    # abi.env is part of the trusted Kaminowaku release payload.
    # shellcheck disable=SC1090
    . "$NOSIX_ABI_ENV"

    [ "$PLATFORM" = "$PLATFORM_TAG" ] \
        || fail "Packaged NOSIX platform '$PLATFORM' does not match target '$PLATFORM_TAG'."
    [ "$ARCH" = "$ARCH_TAG" ] \
        || fail "Packaged NOSIX architecture '$ARCH' does not match target '$ARCH_TAG'."

    validate_abi_name "$LINKER_NAME" "linker name"
    validate_abi_name "$SONAME_NAME" "SONAME"
    validate_abi_name "$REAL_NAME" "real library name"

    [ "$LINKER_NAME" = "libnosix.so" ] \
        || fail "Unexpected packaged NOSIX linker name: $LINKER_NAME"

    for header in nosix.h nosix_poll.h nosix_datagram.h; do
        [ -f "$NOSIX_INCLUDEDIR/$header" ] \
            || fail "Packaged NOSIX header missing: $NOSIX_INCLUDEDIR/$header"
    done

    [ -d "$NOSIX_SOURCE_LIBDIR" ] || fail "Missing packaged NOSIX directory: $NOSIX_SOURCE_LIBDIR"
    [ -f "$NOSIX_SOURCE_LIBDIR/$REAL_NAME" ] || fail "Missing packaged NOSIX ABI: $REAL_NAME"
    # Build linker symlinks under .STAGE, leaving the release payload unchanged.
    mkdir -p "$NOSIX_LIBDIR"
    cp "$NOSIX_SOURCE_LIBDIR/$REAL_NAME" "$NOSIX_LIBDIR/$REAL_NAME"
    rm -f "$NOSIX_LIBDIR/$LINKER_NAME" "$NOSIX_LIBDIR/$SONAME_NAME"
    ln -s "$REAL_NAME" "$NOSIX_LIBDIR/$SONAME_NAME"
    ln -s "$SONAME_NAME" "$NOSIX_LIBDIR/$LINKER_NAME"

    NOSIX_LINKER_NAME="$LINKER_NAME"
    NOSIX_SONAME_NAME="$SONAME_NAME"
    NOSIX_REAL_NAME="$REAL_NAME"

    echo "[nosix] packaged ABI ready: $PLATFORM/$ARCH $REAL_NAME"
}

# The NOSIX RPATH is private; refreshing the global loader cache is unnecessary.

case "$BUILD" in
    debug)
        CFLAGS="-g -O1 -fsanitize=address,leak -w -pthread"
        LDFLAGS="-fsanitize=address,leak -pthread"
        ;;
    release)
        CFLAGS="-O2 -w -pthread"
        LDFLAGS="-pthread"
        ;;
    *)
        fail "Unsupported BUILD '$BUILD'. Use BUILD=debug or BUILD=release."
        ;;
esac

run_make() {
    make \
        CC=clang \
        BUILD="$BUILD" \
        CPPFLAGS="$CPPFLAGS" \
        CFLAGS="$CFLAGS" \
        LDFLAGS="$LDFLAGS" \
        PREFIX="$PREFIX" \
        NOSIX_INCLUDEDIR="$NOSIX_INCLUDEDIR" \
        NOSIX_LIBDIR="$NOSIX_LIBDIR" \
        "$@"
}

check_source_assets() {
    [ -d "$SOURCE_ROOT" ] || fail "Missing Kaminowaku source root: $SOURCE_ROOT"
    [ -f "$SOURCE_ROOT/main.c" ] || fail "Missing core source: $SOURCE_ROOT/main.c"
    [ -f "$SOURCE_ROOT/kaminowaku.c" ] || fail "Missing core source: $SOURCE_ROOT/kaminowaku.c"
    [ -f "$SOURCE_ROOT/data.h" ] || fail "Missing core header: $SOURCE_ROOT/data.h"
    [ -f "$DEFAULT_PROFILE_SRC" ] || fail "Missing restore profile: $DEFAULT_PROFILE_SRC"
    [ -f "./LICENSE.txt" ] || fail "Missing Kaminowaku license: ./LICENSE.txt"

    if [ "$PACKAGED_NOSIX" -eq 1 ]; then
        [ -f "$PACKAGED_NOSIX_LICENSE" ] || fail "Missing packaged NOSIX license: $PACKAGED_NOSIX_LICENSE"
        [ -f "$PACKAGED_NOSIX_MANIFEST" ] || fail "Missing packaged NOSIX manifest: $PACKAGED_NOSIX_MANIFEST"
    fi

    # Phase 14 system Books are permanent runtime assets.
    [ -f "$SYSTEM_BOOK_SSH_KEX" ] || fail "Missing system Book: $SYSTEM_BOOK_SSH_KEX"
    [ -f "$SYSTEM_BOOK_WEB_ENUM" ] || fail "Missing system Book: $SYSTEM_BOOK_WEB_ENUM"

    # books/main is the privileged core Lua interface exposed as kami.*.
    [ -f "$MAIN_TRANSPORT" ] || fail "Missing core Lua module: $MAIN_TRANSPORT"
    [ -f "$MAIN_SEND" ] || fail "Missing core Lua module: $MAIN_SEND"
    [ -f "$MAIN_RECV" ] || fail "Missing core Lua module: $MAIN_RECV"
    [ -f "$MAIN_BYTES" ] || fail "Missing core Lua module: $MAIN_BYTES"
    [ -f "$MAIN_TLS" ] || fail "Missing core Lua module: $MAIN_TLS"
    [ -f "$MAIN_RESULT" ] || fail "Missing core Lua module: $MAIN_RESULT"

    # books/modules is higher-level reusable Book logic with no private _kami access.
    [ -f "$MODULE_SSH" ] || fail "Missing Book module: $MODULE_SSH"
    [ -f "$MODULE_HTTP" ] || fail "Missing Book module: $MODULE_HTTP"
}

check_nosix_abi() {
    ABI_SOURCE="${TMPDIR:-/tmp}/kaminowaku-nosix-abi-$$.c"
    ABI_BINARY="${TMPDIR:-/tmp}/kaminowaku-nosix-abi-$$"

    [ -f "$NOSIX_INCLUDEDIR/nosix.h" ] || fail "NOSIX header missing: $NOSIX_INCLUDEDIR/nosix.h"
    [ -f "$NOSIX_INCLUDEDIR/nosix_poll.h" ] || fail "NOSIX poll header missing: $NOSIX_INCLUDEDIR/nosix_poll.h"
    [ -f "$NOSIX_INCLUDEDIR/nosix_datagram.h" ] || fail "NOSIX datagram header missing: $NOSIX_INCLUDEDIR/nosix_datagram.h"
    [ -e "$NOSIX_LIBDIR/libnosix.so" ] || fail "NOSIX linker library missing: $NOSIX_LIBDIR/libnosix.so"

    trap 'rm -f "$ABI_SOURCE" "$ABI_BINARY"' EXIT HUP INT TERM

    cat > "$ABI_SOURCE" <<'EOF'
#include <nosix.h>
#include <nosix_poll.h>
#include <nosix_datagram.h>
#include <openssl/ssl.h>

int main(void) {
        void *symbols[] = {
                (void*)nosix_init,
                (void*)nosix_close,
                (void*)nosix_capture_reset,
                (void*)nosix_read_timeout,
                (void*)nosix_stream_open,
                (void*)nosix_stream_write,
                (void*)nosix_stream_read,
                (void*)nosix_stream_close,
                (void*)nosix_datagram_open,
                (void*)nosix_datagram_write,
                (void*)nosix_datagram_read,
                (void*)nosix_datagram_close,
                (void*)SSL_CTX_new,
                (void*)TLS_client_method
        };

        return symbols[0] == 0;
}
EOF

    OPENSSL_CFLAGS=$(pkg-config --cflags "$OPENSSL_PKG")
    OPENSSL_LIBS=$(pkg-config --libs "$OPENSSL_PKG")

    # Intentional word splitting: pkg-config returns compiler/linker argument lists.
    # shellcheck disable=SC2086
    clang \
        -I"$NOSIX_INCLUDEDIR" \
        $OPENSSL_CFLAGS \
        "$ABI_SOURCE" \
        -L"$NOSIX_LIBDIR" \
        -lnosix \
        $OPENSSL_LIBS \
        -o "$ABI_BINARY" \
        || fail "NOSIX/OpenSSL ABI link check failed. Verify the shipped ABI and OpenSSL development packages."

    LD_LIBRARY_PATH="$NOSIX_LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$ABI_BINARY" \
        || fail "NOSIX runtime ABI smoke failed to execute. Verify the shipped ABI and loader path."

    rm -f "$ABI_SOURCE" "$ABI_BINARY"
    trap - EXIT HUP INT TERM
}

preflight() {
    # Dependency boundary is intentionally enforced here instead of through
    # a separate test harness: active Kaminowaku links NOSIX + OpenSSL only.
    echo "[check] source/runtime assets"
    check_source_assets
    if [ "$PACKAGED_NOSIX" -eq 1 ]; then
        prepare_packaged_nosix_abi
        echo "[check] packaged NOSIX/OpenSSL ABI"
    else
        echo "[check] installed NOSIX/OpenSSL ABI"
    fi
    check_nosix_abi
    echo "[check] PASS"
}

install_packaged_nosix_abi() {
    [ "$PACKAGED_NOSIX" -eq 1 ] || return 0
    [ "$(id -u)" -eq 0 ] || fail "Packaged NOSIX ABI installation requires root privileges."

    prepare_packaged_nosix_abi

    install -d -m 755 "$INCLUDEDIR"
    install -d -m 755 "$LIBDIR"

    for header in nosix.h nosix_poll.h nosix_datagram.h; do
        install -m 0644 "$NOSIX_INCLUDEDIR/$header" "$INCLUDEDIR/$header"
    done

    install -m 0755 "$NOSIX_LIBDIR/$NOSIX_REAL_NAME" "$LIBDIR/$NOSIX_REAL_NAME"
    ln -sfn "$NOSIX_REAL_NAME" "$LIBDIR/$NOSIX_SONAME_NAME"
    ln -sfn "$NOSIX_SONAME_NAME" "$LIBDIR/$NOSIX_LINKER_NAME"

    refresh_loader_cache

    echo "Installed packaged NOSIX ABI: $LIBDIR/$NOSIX_REAL_NAME"
}

install_runtime_assets() {
    check_source_assets

    install -d -m 755 "$SHARE_DIR"
    install -d -m 755 "$SHARE_PROFILES_DIR"
    install -d -m 755 "$SHARE_TOOLS_DIR"
    install -d -m 755 "$SHARE_BOOKS_DIR"
    install -d -m 755 "$SHARE_BOOKS_MAIN_DIR"
    install -d -m 755 "$SHARE_BOOKS_MODULES_DIR"
    install -d -m 755 "$SHARE_LICENSES_DIR"

    # Remove the superseded system Lua tree. User data under ~/.kaminowaku
    # is intentionally never deleted by the installer.
    if [ -d "$LEGACY_SHARE_LUA_DIR" ]; then
        rm -rf "$LEGACY_SHARE_LUA_DIR"
    fi

    install -m 644 "$DEFAULT_PROFILE_SRC" "$DEFAULT_PROFILE_DST"
    install -m 644 "./LICENSE.txt" "$SHARE_LICENSES_DIR/KAMINOWAKU-LICENSE.txt"

    if [ "$PACKAGED_NOSIX" -eq 1 ]; then
        install -m 644 "$PACKAGED_NOSIX_LICENSE" "$SHARE_LICENSES_DIR/NOSIX-LICENSE.txt"
        install -m 644 "$PACKAGED_NOSIX_MANIFEST" "$SHARE_LICENSES_DIR/NOSIX-BUILD-MANIFEST.txt"
    fi

    # System Books are permanent Lua runtime assets. The interpreter is
    # Kaminowaku-owned C code; this does not install or depend on external Lua.
    if [ -d "$BOOKS_SRC_DIR" ]; then
        find "$BOOKS_SRC_DIR" -type f -name '*.lua' | while IFS= read -r src; do
            rel=${src#./books/}
            dst="${SHARE_BOOKS_DIR}/${rel}"
            install -d -m 755 "$(dirname "$dst")"
            install -m 644 "$src" "$dst"
        done
    fi

}

case "$TARGET" in
    check)
        preflight
        ;;

    install)
        [ "$(id -u)" -eq 0 ] || fail "The install target requires root privileges. Run with sudo."
        preflight
        echo "[make] clean"
        run_make clean
        echo "[nosix] install packaged ABI when present"
        install_packaged_nosix_abi
        echo "[make] build through .STAGE ($BUILD)"
        run_make all
        echo "[make] install"
        install -d -m 755 "$BINDIR"
        install -m 0755 "$STAGE_ROOT/bin/kaminowaku" "$BINDIR/kaminowaku"
        echo "Installed to $BINDIR/kaminowaku"
        echo "[assets] install runtime assets"
        install_runtime_assets
        echo "Installed binary: ${BINDIR}/kaminowaku"
        echo "Installed restore profile: ${DEFAULT_PROFILE_DST}"
        echo "Installed system books: ${SHARE_BOOKS_DIR}"
        echo "Installed licenses: ${SHARE_LICENSES_DIR}"
        echo "Kaminowaku has been installed."
        ;;

    all)
        preflight
        echo "[make] clean"
        run_make clean
        echo "[make] build through .STAGE ($BUILD)"
        run_make all
        echo "DONE."
        ;;

    clean)
        run_make clean
        echo "DONE."
        ;;

    info)
        echo "CC=clang"
        echo "BUILD=$BUILD"
        echo "OS=$UNAME_S"
        echo "ARCH=$UNAME_M"
        echo "CPPFLAGS=$CPPFLAGS"
        echo "CFLAGS=$CFLAGS"
        echo "LDFLAGS=$LDFLAGS"
        echo "PREFIX=$PREFIX"
        echo "BINDIR=$BINDIR"
        echo "INCLUDEDIR=$INCLUDEDIR"
        echo "LIBDIR=$LIBDIR"
        echo "PLATFORM_TAG=$PLATFORM_TAG"
        echo "ARCH_TAG=$ARCH_TAG"
        echo "PACKAGED_NOSIX=$PACKAGED_NOSIX"
        echo "NOSIX_INCLUDEDIR=$NOSIX_INCLUDEDIR"
        echo "NOSIX_LIBDIR=$NOSIX_LIBDIR"
        echo "SHARE_DIR=$SHARE_DIR"
        echo "SHARE_PROFILES_DIR=$SHARE_PROFILES_DIR"
        echo "SHARE_TOOLS_DIR=$SHARE_TOOLS_DIR"
        echo "SHARE_BOOKS_DIR=$SHARE_BOOKS_DIR"
        echo "SHARE_BOOKS_MAIN_DIR=$SHARE_BOOKS_MAIN_DIR"
        echo "SHARE_BOOKS_MODULES_DIR=$SHARE_BOOKS_MODULES_DIR"
        echo "SHARE_LICENSES_DIR=$SHARE_LICENSES_DIR"
        echo "LEGACY_SHARE_LUA_DIR=$LEGACY_SHARE_LUA_DIR"
        echo "DEFAULT_PROFILE_SRC=$DEFAULT_PROFILE_SRC"
        echo "DEFAULT_PROFILE_DST=$DEFAULT_PROFILE_DST"
        echo "BOOKS_SRC_DIR=$BOOKS_SRC_DIR"
        echo "SOURCE_ROOT=$SOURCE_ROOT"
        echo "STAGE_ROOT=$STAGE_ROOT"
        echo "SYSTEM_BOOK_SSH_KEX=$SYSTEM_BOOK_SSH_KEX"
        echo "SYSTEM_BOOK_WEB_ENUM=$SYSTEM_BOOK_WEB_ENUM"
        echo "MAIN_TRANSPORT=$MAIN_TRANSPORT"
        echo "MAIN_SEND=$MAIN_SEND"
        echo "MAIN_RECV=$MAIN_RECV"
        echo "MAIN_BYTES=$MAIN_BYTES"
        echo "MAIN_TLS=$MAIN_TLS"
        echo "MAIN_RESULT=$MAIN_RESULT"
        echo "MODULE_SSH=$MODULE_SSH"
        echo "MODULE_HTTP=$MODULE_HTTP"
        echo "OPENSSL_PKG=$OPENSSL_PKG"
        echo "NOSIX_ABI_ENV=$NOSIX_ABI_ENV"
        echo "NOSIX_LINKER_NAME=$NOSIX_LINKER_NAME"
        echo "NOSIX_SONAME_NAME=$NOSIX_SONAME_NAME"
        echo "NOSIX_REAL_NAME=$NOSIX_REAL_NAME"
        echo ""
        echo "Makefile configuration:"
        run_make info
        ;;
esac
