# qport: host build (macOS / Linux) and QNX OS 8.0 cross build.
#
#   make                 build ./qport for this host
#   make test            build and run the test suite
#   make qnx             cross build for QNX 8.0 with qcc (needs the QNX SDP:
#                        source ~/qnx800/qnxsdp-env.sh first)
#   make qnx QNX_ARCH=x86_64     (default aarch64le)

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE

SRC  = src/main.c src/scan.c src/lexer.c src/rules.c src/report.c src/emit.c src/util.c src/html_template.c
LIB  = $(filter-out src/main.c,$(SRC))
HDR  = src/qport.h

QNX_ARCH ?= aarch64le
QNX_OUT   = build-qnx-$(QNX_ARCH)

.PHONY: all test clean qnx qnx-test

all: qport

qport: $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRC)

# The HTML report template is compiled in. src/html_template.c is committed
# so a QNX build needs no host helper; it is regenerated when the template changes.
src/html_template.c: report/template.html tools/embed.c
	$(CC) -o tools/embed tools/embed.c
	./tools/embed report/template.html $@

tests/unit: tests/unit.c $(LIB) $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -o $@ tests/unit.c $(LIB)

test: qport tests/unit
	./tests/unit
	sh tests/run.sh ./qport

# QNX OS 8.0: qcc picks the target with -V (QNX Porting Guide, "Make").
# _QNX_SOURCE exposes the full QNX API set.
qnx: $(SRC) $(HDR)
	@test -n "$$QNX_TARGET" || { echo "QNX_TARGET is not set: source ~/qnx800/qnxsdp-env.sh first"; exit 1; }
	mkdir -p $(QNX_OUT)
	qcc -Vgcc_nto$(QNX_ARCH) -D_QNX_SOURCE -O2 -g -Wall -o $(QNX_OUT)/qport $(SRC)
	qcc -Vgcc_nto$(QNX_ARCH) -D_QNX_SOURCE -O2 -g -Wall -Isrc -o $(QNX_OUT)/unit tests/unit.c $(LIB)
	@echo "built $(QNX_OUT)/qport and $(QNX_OUT)/unit for QNX OS 8.0 $(QNX_ARCH)"

# Host-side check of the QNX build: scan the tree against the real SDP headers.
qnx-test: qport
	@test -n "$$QNX_TARGET" || { echo "QNX_TARGET is not set: source ~/qnx800/qnxsdp-env.sh first"; exit 1; }
	./qport scan --sysroot "$$QNX_TARGET" src
	./qport scan --sysroot "$$QNX_TARGET" examples/linux-telemetryd --fail-on never

clean:
	rm -rf qport tests/unit tests/out build-qnx-* tools/embed
