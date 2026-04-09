# ════════════════════════════════════════════════════════════════════════
#  Makefile — Page-Aware Slab Allocator
#  Builds both the original (4 KB pages) and the huge-page variant.
# ════════════════════════════════════════════════════════════════════════

CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -Wpedantic \
           -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L \
           -std=c11
LDFLAGS := -lm

# ── output binary ────────────────────────────────────────────────────────
BENCH       := bench_page_slab

# ── source files ─────────────────────────────────────────────────────────
SRCS        := hugepage_slab.c bench_page_slab.c

# ── default: build ───────────────────────────────────────────────────────
.PHONY: all clean run run-orig run-hp setup-hugepages check-hugepages help

all: $(BENCH)

# ── benchmark ────────────────────────────────────────────────────────────
$(BENCH): $(SRCS) hugepage_slab.h
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)
	@echo "  built: $@"

# ── run ──────────────────────────────────────────────────────────────────
run: $(BENCH)
	@echo ""
	@echo "════════════════════════════════════════════════════════════════"
	@echo "  Running page-aware huge-page slab benchmark"
	@echo "════════════════════════════════════════════════════════════════"
	./$(BENCH)

run-orig: run
run-hp: run

# ── run as root to unlock full /proc/pagemap physical addresses ──────────
run-hp-root: $(BENCH)
	@echo "  Running with sudo for /proc/pagemap physical page visibility..."
	sudo ./$(BENCH)

# ── huge page pool management ────────────────────────────────────────────
#
#  MAP_HUGETLB requires pre-reserved pages in the kernel pool.
#  Reserve N huge pages (default N=32, each = 2 MB → 64 MB total).
#
#  Usage:  make setup-hugepages N=64
#          make check-hugepages
#
N ?= 32

setup-hugepages:
	@echo "  Reserving $(N) huge pages ($(shell echo $$(( $(N) * 2 )))  MB)..."
	@echo "  (requires sudo)"
	sudo sh -c 'echo $(N) > /proc/sys/vm/nr_hugepages'
	@$(MAKE) check-hugepages

check-hugepages:
	@echo ""
	@echo "  ── Huge page status ─────────────────────────────────────────"
	@grep -E 'HugePages|Hugepagesize' /proc/meminfo | sed 's/^/  /'
	@echo ""
	@echo "  ── THP policy ───────────────────────────────────────────────"
	@if [ -f /sys/kernel/mm/transparent_hugepage/enabled ]; then \
	    echo "  THP: $$(cat /sys/kernel/mm/transparent_hugepage/enabled)"; \
	else \
	    echo "  THP: not available"; \
	fi
	@echo ""

# ── enable THP (Transparent Huge Pages) if not already on ───────────────
enable-thp:
	@echo "  Enabling THP (madvise mode)..."
	sudo sh -c 'echo madvise > /sys/kernel/mm/transparent_hugepage/enabled'
	@cat /sys/kernel/mm/transparent_hugepage/enabled

# ── clean ────────────────────────────────────────────────────────────────
clean:
	rm -f $(BENCH_ORIG) $(BENCH_HP) *.o

# ── help ─────────────────────────────────────────────────────────────────
help:
	@echo ""
	@echo "  Targets:"
	@echo "    make                   — build both benchmarks"
	@echo "    make run               — build and run both"
	@echo "    make run-orig          — run original (4 KB page) benchmark"
	@echo "    make run-hp            — run huge-page benchmark"
	@echo "    make run-hp-root       — run with sudo (full pagemap)"
	@echo "    make setup-hugepages [N=32]  — reserve N huge pages"
	@echo "    make check-hugepages   — show current huge page pool status"
	@echo "    make enable-thp        — set THP to madvise mode"
	@echo "    make clean             — remove binaries"
	@echo ""
	@echo "  Quick start (no root needed — THP fallback):"
	@echo "    make && make run-hp"
	@echo ""
	@echo "  For MAP_HUGETLB (faster, guaranteed huge pages):"
	@echo "    make setup-hugepages N=64"
	@echo "    make run-hp"
	@echo ""
	@echo "  For full /proc/pagemap physical addresses in Experiment 6:"
	@echo "    make run-hp-root"
	@echo ""
