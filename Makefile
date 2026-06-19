CC = gcc

BUILD_TYPE ?= release

BASE_CFLAGS = -std=c2x -Iinclude -Ilib -Iprotocols
BASE_CFLAGS += -Wall -Wextra -Wpedantic -Wformat=2 -Wstrict-prototypes
BASE_CFLAGS += -Wmissing-prototypes -Wold-style-definition -Wredundant-decls
BASE_CFLAGS += -Wnested-externs -Wmissing-include-dirs -Wlogical-op
BASE_CFLAGS += -Wjump-misses-init -Wdouble-promotion -Wshadow
BASE_CFLAGS += -fstack-protector-strong

DEBUG_CFLAGS = $(BASE_CFLAGS) -g3 -O0 -DDEBUG -fsanitize=address -fsanitize=undefined
DEBUG_LDFLAGS = -fsanitize=address -fsanitize=undefined

RELEASE_CFLAGS = $(BASE_CFLAGS) -O3 -DNDEBUG -flto -fPIE -D_FORTIFY_SOURCE=2

ifeq ($(BUILD_TYPE),debug)
    CFLAGS = $(DEBUG_CFLAGS)
    LDFLAGS = -lwayland-client -lm -lpthread $(DEBUG_LDFLAGS)
else
    CFLAGS = $(RELEASE_CFLAGS)
    LDFLAGS = -lwayland-client -lm -lpthread -flto -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack
endif

SRCDIR = src
INCDIR = include
BUILDDIR = build
OBJDIR = $(BUILDDIR)/obj
PROTOCOLDIR = protocols

SOURCES = $(shell find $(SRCDIR) -name "*.c")
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

EMBED_SCRIPT = scripts/embed_assets.sh
EMBEDDED_ASSETS_H = $(INCDIR)/graphics/embedded_assets.h
EMBEDDED_ASSETS_C = $(SRCDIR)/graphics/embedded_assets.c

C_PROTOCOL_SRC = $(PROTOCOLDIR)/zwlr-layer-shell-v1-protocol.c $(PROTOCOLDIR)/xdg-shell-protocol.c $(PROTOCOLDIR)/wlr-foreign-toplevel-management-v1-protocol.c $(PROTOCOLDIR)/xdg-output-unstable-v1-protocol.c $(PROTOCOLDIR)/fractional-scale-v1-protocol.c $(PROTOCOLDIR)/viewporter-protocol.c
H_PROTOCOL_HDR = $(PROTOCOLDIR)/zwlr-layer-shell-v1-client-protocol.h $(PROTOCOLDIR)/xdg-shell-client-protocol.h $(PROTOCOLDIR)/wlr-foreign-toplevel-management-v1-client-protocol.h $(PROTOCOLDIR)/xdg-output-unstable-v1-client-protocol.h $(PROTOCOLDIR)/fractional-scale-v1-client-protocol.h $(PROTOCOLDIR)/viewporter-client-protocol.h
PROTOCOL_OBJECTS = $(C_PROTOCOL_SRC:$(PROTOCOLDIR)/%.c=$(OBJDIR)/%.o)

TARGET = $(BUILDDIR)/bongocat

.PHONY: all clean distclean protocols embed-assets format format-check lint

all: $(TARGET)

embed-assets: 
	./$(EMBED_SCRIPT)

$(OBJDIR):
	mkdir -p $(OBJDIR)
	mkdir -p $(OBJDIR)/core
	mkdir -p $(OBJDIR)/graphics
	mkdir -p $(OBJDIR)/platform
	mkdir -p $(OBJDIR)/config
	mkdir -p $(OBJDIR)/utils
	mkdir -p $(BUILDDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(PROTOCOLDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS) $(PROTOCOL_OBJECTS)
	$(CC) $(OBJECTS) $(PROTOCOL_OBJECTS) -o $(TARGET) $(LDFLAGS)

protocols:
	@command -v wayland-scanner >/dev/null 2>&1 || { \
		echo "ERROR: wayland-scanner not found."; \
		echo "Install it with your package manager (e.g. 'pacman -S wayland', 'apt install libwayland-bin')."; \
		exit 1; \
	}
	wayland-scanner client-header $(PROTOCOLDIR)/xdg-shell.xml $(PROTOCOLDIR)/xdg-shell-client-protocol.h
	wayland-scanner private-code $(PROTOCOLDIR)/xdg-shell.xml $(PROTOCOLDIR)/xdg-shell-protocol.c
	wayland-scanner private-code $(PROTOCOLDIR)/wlr-layer-shell-unstable-v1.xml $(PROTOCOLDIR)/zwlr-layer-shell-v1-protocol.c
	wayland-scanner client-header $(PROTOCOLDIR)/wlr-layer-shell-unstable-v1.xml $(PROTOCOLDIR)/zwlr-layer-shell-v1-client-protocol.h
	wayland-scanner private-code $(PROTOCOLDIR)/wlr-foreign-toplevel-management-unstable-v1.xml $(PROTOCOLDIR)/wlr-foreign-toplevel-management-v1-protocol.c
	wayland-scanner client-header $(PROTOCOLDIR)/wlr-foreign-toplevel-management-unstable-v1.xml $(PROTOCOLDIR)/wlr-foreign-toplevel-management-v1-client-protocol.h
	wayland-scanner client-header $(PROTOCOLDIR)/xdg-output-unstable-v1.xml $(PROTOCOLDIR)/xdg-output-unstable-v1-client-protocol.h
	wayland-scanner private-code $(PROTOCOLDIR)/xdg-output-unstable-v1.xml $(PROTOCOLDIR)/xdg-output-unstable-v1-protocol.c
	wayland-scanner client-header $(PROTOCOLDIR)/fractional-scale-v1.xml $(PROTOCOLDIR)/fractional-scale-v1-client-protocol.h
	wayland-scanner private-code $(PROTOCOLDIR)/fractional-scale-v1.xml $(PROTOCOLDIR)/fractional-scale-v1-protocol.c
	wayland-scanner client-header $(PROTOCOLDIR)/viewporter.xml $(PROTOCOLDIR)/viewporter-client-protocol.h
	wayland-scanner private-code $(PROTOCOLDIR)/viewporter.xml $(PROTOCOLDIR)/viewporter-protocol.c

clean:
	rm -rf $(BUILDDIR)

distclean: clean
	rm -f $(C_PROTOCOL_SRC) $(H_PROTOCOL_HDR)

debug:
	$(MAKE) BUILD_TYPE=debug

release:
	$(MAKE) BUILD_TYPE=release

install: $(TARGET)
	install -D $(TARGET) $(DESTDIR)/usr/local/bin/bongocat
	install -D bongocat.conf.example $(DESTDIR)/usr/local/share/bongocat/bongocat.conf.example
	install -D scripts/find_input_devices.sh $(DESTDIR)/usr/local/bin/bongocat-find-devices
	install -D man/bongocat.1 $(DESTDIR)/usr/local/share/man/man1/bongocat.1

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/bongocat
	rm -f $(DESTDIR)/usr/local/bin/bongocat-find-devices
	rm -rf $(DESTDIR)/usr/local/share/bongocat

memcheck: debug
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET)

release-local:
	$(MAKE) BUILD_TYPE=release RELEASE_CFLAGS="$(RELEASE_CFLAGS) -march=native"

tsan:
	$(MAKE) BUILD_TYPE=debug DEBUG_CFLAGS="$(BASE_CFLAGS) -g3 -O1 -DDEBUG -fsanitize=thread" DEBUG_LDFLAGS="-fsanitize=thread"

profile: release
	perf record -g ./$(TARGET)
	perf report

.PHONY: debug release release-local tsan install uninstall analyze memcheck profile format format-check lint test


PROJECT_SOURCES = $(shell find $(SRCDIR) -name '*.c' ! -path '*/embedded_assets.c')
PROJECT_HEADERS = $(shell find $(INCDIR) -name '*.h')
ALL_PROJECT_FILES = $(PROJECT_SOURCES) $(PROJECT_HEADERS)

format:
	@echo "Formatting source files..."
	@clang-format -i $(ALL_PROJECT_FILES)
	@echo "Done! Formatted $(words $(ALL_PROJECT_FILES)) files."

format-check:
	@echo "Checking code formatting..."
	@clang-format --dry-run --Werror $(ALL_PROJECT_FILES)
	@echo "All files are properly formatted."

lint:
	@echo "Running static analysis..."
	@clang-tidy $(PROJECT_SOURCES) -- $(CFLAGS)
	@echo "Static analysis complete."

analyze: lint

compiledb: clean
	@echo "Generating compile_commands.json..."
	@bear -- $(MAKE) all 2>/dev/null || (echo "Note: 'bear' not installed. Install with: sudo pacman -S bear" && false)
	@echo "compile_commands.json generated!"


TESTDIR = tests
TEST_CFLAGS = $(BASE_CFLAGS) -g3 -O0 -DDEBUG -DTEST_BUILD
TEST_LDFLAGS = -lm -lpthread

CONFIG_TEST_DEPS = src/config/config.c src/utils/error.c src/utils/memory.c

MEMORY_TEST_DEPS = src/utils/memory.c src/utils/error.c

$(BUILDDIR)/test_config: $(TESTDIR)/test_config.c $(CONFIG_TEST_DEPS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $^ -o $@ $(TEST_LDFLAGS)

$(BUILDDIR)/test_memory: $(TESTDIR)/test_memory.c $(MEMORY_TEST_DEPS) | $(OBJDIR)
	$(CC) $(TEST_CFLAGS) $^ -o $@ $(TEST_LDFLAGS)

TEST_BINARIES = $(BUILDDIR)/test_config $(BUILDDIR)/test_memory

test: $(TEST_BINARIES)
	@echo "Running tests..."
	@failures=0; \
	for t in $(TEST_BINARIES); do \
		echo "--- $$(basename $$t) ---"; \
		$$t || failures=$$((failures + 1)); \
	done; \
	if [ $$failures -gt 0 ]; then \
		echo "$$failures test suite(s) failed"; \
		exit 1; \
	fi; \
	echo "All tests passed."

.PHONY: compiledb test
