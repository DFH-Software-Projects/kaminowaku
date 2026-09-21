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
PKG_CONFIG ?= pkg-config

# ---- Prefix ----
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib
NOSIX_INCLUDEDIR ?= $(INCLUDEDIR)
NOSIX_LIBDIR ?= $(LIBDIR)

# ---- Runtime dependency discovery ----
OPENSSL_PKG != sh -c 'if command -v $(PKG_CONFIG) >/dev/null 2>&1 && $(PKG_CONFIG) --exists openssl 2>/dev/null; then echo openssl; fi'
OPENSSL_CFLAGS != sh -c 'if [ -n "$(OPENSSL_PKG)" ]; then $(PKG_CONFIG) --cflags $(OPENSSL_PKG) 2>/dev/null; fi'
OPENSSL_LIBS != sh -c 'if [ -n "$(OPENSSL_PKG)" ]; then $(PKG_CONFIG) --libs $(OPENSSL_PKG) 2>/dev/null; fi'

CPPFLAGS ?=
CFLAGS ?= -g -O1 -fsanitize=address,leak -Wall -Wextra -pthread
LDFLAGS ?= -fsanitize=address,leak -pthread
LDLIBS ?=

KAMI_CPPFLAGS = -iquote $(STAGE_INCLUDE) -I$(NOSIX_INCLUDEDIR) $(OPENSSL_CFLAGS)
KAMI_LDFLAGS = -L$(NOSIX_LIBDIR)
KAMI_LDLIBS = -lnosix $(OPENSSL_LIBS)

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
	@if [ -z "$(OPENSSL_PKG)" ]; then \
		echo "ERROR: OpenSSL development package/pkg-config metadata not found."; \
		echo "Expected pkg-config module: openssl."; \
		exit 1; \
	fi
	@if [ ! -f "$(NOSIX_INCLUDEDIR)/nosix.h" ]; then \
		echo "ERROR: Missing installed NOSIX header: $(NOSIX_INCLUDEDIR)/nosix.h"; \
		exit 1; \
	fi
	@if [ ! -f "$(NOSIX_INCLUDEDIR)/nosix_poll.h" ]; then \
		echo "ERROR: Missing installed NOSIX header: $(NOSIX_INCLUDEDIR)/nosix_poll.h"; \
		exit 1; \
	fi
	@if [ ! -f "$(NOSIX_INCLUDEDIR)/nosix_datagram.h" ]; then \
		echo "ERROR: Missing installed NOSIX datagram ABI: $(NOSIX_INCLUDEDIR)/nosix_datagram.h"; \
		echo "Reinstall current NOSIX before building Kaminowaku."; \
		exit 1; \
	fi
	@if [ ! -e "$(NOSIX_LIBDIR)/libnosix.so" ]; then \
		echo "ERROR: Missing installed NOSIX library: $(NOSIX_LIBDIR)/libnosix.so"; \
		exit 1; \
	fi

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
	echo "[STAGE] projected headers and build manifests"

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
	@echo "NOSIX_LIBDIR=$(NOSIX_LIBDIR)"
	@echo "OPENSSL_PKG=$(OPENSSL_PKG)"
	@echo "OPENSSL_CFLAGS=$(OPENSSL_CFLAGS)"
	@echo "OPENSSL_LIBS=$(OPENSSL_LIBS)"
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
