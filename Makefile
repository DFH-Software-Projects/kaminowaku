# Kaminowaku Makefile
# GNU make / BSD make compatible

TARGET = kaminowaku

# ---- Source/build projection ----
SOURCE_ROOT ?= src
STAGE_ROOT ?= .STAGE
STAGE_INCLUDE ?= $(STAGE_ROOT)/include
STAGE_OBJ ?= $(STAGE_ROOT)/obj
STAGE_BIN ?= $(STAGE_ROOT)/bin
STAGE_META ?= $(STAGE_ROOT)/meta
SOURCE_MANIFEST ?= $(STAGE_META)/sources.list
OBJECT_MANIFEST ?= $(STAGE_META)/objects.list
TARGET_PATH ?= $(STAGE_BIN)/$(TARGET)

# Source/object discovery is performed by POSIX shell recipes rather than
# parse-time make shell assignments. This keeps GNU make and BSD make aligned
# and avoids make-time expansion of shell regex/metacharacters.

# ---- Toolchain ----
CC ?= cc
BUILD ?= debug
PLATFORM_TAG != uname -s | tr '[:upper:]' '[:lower:]'

# ---- Prefix ----
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib

# ---- Repository-local dependencies; no package-manager discovery ----
NOSIX_ROOT ?= libs/nosix
NOSIX_INCLUDEDIR ?= $(NOSIX_ROOT)/include
NOSIX_PACKAGE_LIBDIR ?= $(NOSIX_ROOT)/$(PLATFORM_TAG)/lib
NOSIX_ABI_ENV ?= $(NOSIX_ROOT)/$(PLATFORM_TAG)/abi.env
STAGE_PRIVATE_LIB ?= $(STAGE_ROOT)/lib/kaminowaku
OPENSSL_ROOT ?= libs/openssl
OPENSSL_INCLUDEDIR ?= $(OPENSSL_ROOT)/$(PLATFORM_TAG)/include
OPENSSL_LIBDIR ?= $(OPENSSL_ROOT)/$(PLATFORM_TAG)/lib
OPENSSL_SYSTEM_LIBS != sh -c 'case "$(uname -s)" in Linux) echo -ldl;; *) echo "";; esac'

CPPFLAGS ?=
CFLAGS ?= -g -O1 -fsanitize=address,leak -Wall -Wextra -pthread
LDFLAGS ?= -fsanitize=address,leak -pthread
LDLIBS ?=

KAMI_CPPFLAGS = -iquote $(STAGE_INCLUDE) -I$(NOSIX_INCLUDEDIR) -I$(OPENSSL_INCLUDEDIR)
# Both .STAGE/bin and PREFIX/bin resolve their own private NOSIX runtime.
KAMI_LDFLAGS = -L$(STAGE_PRIVATE_LIB) -Wl,-z,origin -Wl,-rpath,'$ORIGIN/../lib/kaminowaku'
KAMI_LDLIBS = -lnosix $(OPENSSL_LIBDIR)/libssl.a $(OPENSSL_LIBDIR)/libcrypto.a $(OPENSSL_SYSTEM_LIBS)

all: runtime-check
	@$(MAKE) prepare-stage
	@$(MAKE) build
	@$(MAKE) finalize-stage

build: stage-check objects
	@set -eu; \
	OBJS=$$(cat "$(OBJECT_MANIFEST)"); \
	[ -n "$$OBJS" ] || { echo "ERROR: Empty object manifest: $(OBJECT_MANIFEST)"; exit 1; }; \
	mkdir -p "$(STAGE_BIN)"; \
	echo "[LD] $(TARGET_PATH)"; \
	$(CC) $$OBJS $(LDFLAGS) $(KAMI_LDFLAGS) $(LDLIBS) $(KAMI_LDLIBS) -o "$(TARGET_PATH)"

runtime-check:
	@set -eu; \
	[ -f "$(NOSIX_INCLUDEDIR)/nosix.h" ] || { echo "ERROR: Missing packaged NOSIX headers."; exit 1; }; \
	[ -f "$(NOSIX_ABI_ENV)" ] || { echo "ERROR: Missing NOSIX ABI metadata."; exit 1; }; \
	[ -f "$(OPENSSL_INCLUDEDIR)/openssl/ssl.h" ] || { echo "ERROR: Missing OpenSSL headers for $(PLATFORM_TAG)."; exit 1; }; \
	[ -f "$(OPENSSL_INCLUDEDIR)/openssl/configuration.h" ] || { echo "ERROR: Missing OpenSSL generated configuration for $(PLATFORM_TAG)."; exit 1; }; \
	[ -s "$(OPENSSL_LIBDIR)/libssl.a" ] || { echo "ERROR: Missing libssl.a for $(PLATFORM_TAG)."; exit 1; }; \
	[ -s "$(OPENSSL_LIBDIR)/libcrypto.a" ] || { echo "ERROR: Missing libcrypto.a for $(PLATFORM_TAG)."; exit 1; }

prepare-stage:
	@set -eu; \
	[ -d "$(SOURCE_ROOT)" ] || { echo "ERROR: Missing source root: $(SOURCE_ROOT)"; exit 1; }; \
	mkdir -p "$(STAGE_OBJ)" "$(STAGE_BIN)" "$(STAGE_META)"; \
	rm -rf "$(STAGE_INCLUDE)"; \
	mkdir -p "$(STAGE_INCLUDE)"; \
	DUP_HEADERS=$$(find "$(SOURCE_ROOT)" -type f -name '*.h' -exec basename {} \; | LC_ALL=C sort | uniq -d); \
	if [ -n "$$DUP_HEADERS" ]; then \
		echo "ERROR: Duplicate project header basenames cannot be projected into $(STAGE_INCLUDE):"; \
		printf '%s\n' "$$DUP_HEADERS"; \
		exit 1; \
	fi; \
	find "$(SOURCE_ROOT)" -type f -name '*.h' -print | LC_ALL=C sort | while IFS= read -r HEADER; do \
		cp "$$HEADER" "$(STAGE_INCLUDE)/$$(basename "$$HEADER")"; \
	done; \
	find "$(SOURCE_ROOT)" -type f -name '*.c' -print | LC_ALL=C sort > "$(SOURCE_MANIFEST)"; \
	: > "$(OBJECT_MANIFEST)"; \
	while IFS= read -r SRC; do \
		REL=$${SRC#$(SOURCE_ROOT)/}; \
		printf '%s\n' "$(STAGE_OBJ)/$${REL%.c}.o" >> "$(OBJECT_MANIFEST)"; \
	done < "$(SOURCE_MANIFEST)"; \
\techo "[STAGE] projected headers and build manifests"
	@set -eu; \
	[ -f "$(NOSIX_ABI_ENV)" ] || { echo "ERROR: Missing $(NOSIX_ABI_ENV)."; exit 1; }; \
	. "./$(NOSIX_ABI_ENV)"; \
	[ "$PLATFORM" = "$(PLATFORM_TAG)" ] || { echo "ERROR: NOSIX platform mismatch."; exit 1; }; \
	[ "$ARCH" = "amd64" ] || { echo "ERROR: Unsupported NOSIX architecture."; exit 1; }; \
	[ "$LINKER_NAME" = "libnosix.so" ] || { echo "ERROR: Unexpected NOSIX linker name."; exit 1; }; \
	[ -s "$(NOSIX_PACKAGE_LIBDIR)/$REAL_NAME" ] || { echo "ERROR: NOSIX library missing."; exit 1; }; \
	mkdir -p "$(STAGE_PRIVATE_LIB)"; \
	cp "$(NOSIX_PACKAGE_LIBDIR)/$REAL_NAME" "$(STAGE_PRIVATE_LIB)/$REAL_NAME"; \
	ln -sfn "$REAL_NAME" "$(STAGE_PRIVATE_LIB)/$SONAME_NAME"; \
	ln -sfn "$SONAME_NAME" "$(STAGE_PRIVATE_LIB)/$LINKER_NAME"; \
	echo "[STAGE] packaged NOSIX runtime staged"

stage-check:
	@[ -d "$(STAGE_INCLUDE)" ] || { echo "ERROR: .STAGE header projection missing. Run 'make prepare-stage' or 'make all'."; exit 1; }
	@[ -f "$(SOURCE_MANIFEST)" ] || { echo "ERROR: .STAGE source manifest missing. Run 'make prepare-stage' or 'make all'."; exit 1; }
	@[ -f "$(OBJECT_MANIFEST)" ] || { echo "ERROR: .STAGE object manifest missing. Run 'make prepare-stage' or 'make all'."; exit 1; }

objects: stage-check
	@set -eu; \
	while IFS= read -r SRC; do \
		REL=$${SRC#$(SOURCE_ROOT)/}; \
		OBJ="$(STAGE_OBJ)/$${REL%.c}.o"; \
		mkdir -p "$$(dirname "$$OBJ")"; \
		REBUILD=0; \
		if [ ! -f "$$OBJ" ] || [ "$$SRC" -nt "$$OBJ" ]; then REBUILD=1; fi; \
		if [ "$$REBUILD" -eq 0 ] && find "$(SOURCE_ROOT)" -type f -name '*.h' -newer "$$OBJ" -print -quit | grep -q .; then REBUILD=1; fi; \
		if [ "$$REBUILD" -eq 1 ]; then \
			echo "[CC] $$SRC"; \
			$(CC) $(CPPFLAGS) $(KAMI_CPPFLAGS) $(CFLAGS) -c "$$SRC" -o "$$OBJ"; \
		fi; \
	done < "$(SOURCE_MANIFEST)"

finalize-stage:
	@rm -rf "$(STAGE_INCLUDE)"
	@echo "[STAGE] removed temporary header projection; objects retained"

install: all
	install -d $(BINDIR)
	install -m 0755 "$(TARGET_PATH)" "$(BINDIR)/$(TARGET)"
	@echo "Installed to $(BINDIR)/$(TARGET)"

clean:
	rm -rf "$(STAGE_ROOT)"
	rm -f ./*.o ./$(TARGET)

info:
	@echo "CC=$(CC)"
	@echo "BUILD=$(BUILD)"
	@echo "PREFIX=$(PREFIX)"
	@echo "INCLUDEDIR=$(INCLUDEDIR)"
	@echo "LIBDIR=$(LIBDIR)"
	@echo "NOSIX_INCLUDEDIR=$(NOSIX_INCLUDEDIR)"
	@echo "PLATFORM_TAG=$(PLATFORM_TAG)"
	@echo "NOSIX_PACKAGE_LIBDIR=$(NOSIX_PACKAGE_LIBDIR)"
	@echo "STAGE_PRIVATE_LIB=$(STAGE_PRIVATE_LIB)"
	@echo "OPENSSL_INCLUDEDIR=$(OPENSSL_INCLUDEDIR)"
	@echo "OPENSSL_LIBDIR=$(OPENSSL_LIBDIR)"
	@echo "CPPFLAGS=$(CPPFLAGS)"
	@echo "KAMI_CPPFLAGS=$(KAMI_CPPFLAGS)"
	@echo "CFLAGS=$(CFLAGS)"
	@echo "LDFLAGS=$(LDFLAGS)"
	@echo "KAMI_LDFLAGS=$(KAMI_LDFLAGS)"
	@echo "LDLIBS=$(LDLIBS)"
	@echo "KAMI_LDLIBS=$(KAMI_LDLIBS)"
	@echo "SOURCE_ROOT=$(SOURCE_ROOT)"
	@echo "STAGE_ROOT=$(STAGE_ROOT)"
	@echo "STAGE_INCLUDE=$(STAGE_INCLUDE)"
	@echo "STAGE_OBJ=$(STAGE_OBJ)"
	@echo "STAGE_BIN=$(STAGE_BIN)"
	@echo "STAGE_META=$(STAGE_META)"
	@echo "SOURCE_MANIFEST=$(SOURCE_MANIFEST)"
	@echo "OBJECT_MANIFEST=$(OBJECT_MANIFEST)"
	@echo "TARGET_PATH=$(TARGET_PATH)"
	@printf 'SRCS='; find "$(SOURCE_ROOT)" -type f -name '*.c' -print 2>/dev/null | LC_ALL=C sort | paste -sd ' ' -; printf '\n'
	@printf 'OBJS='; find "$(SOURCE_ROOT)" -type f -name '*.c' -print 2>/dev/null | LC_ALL=C sort | while IFS= read -r SRC; do REL=$${SRC#$(SOURCE_ROOT)/}; printf '%s ' "$(STAGE_OBJ)/$${REL%.c}.o"; done; printf '\n'

.PHONY: all build runtime-check prepare-stage stage-check objects finalize-stage install clean info
