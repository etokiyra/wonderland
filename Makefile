# Wonderland build.
#
# Targets: all (default), test, asan, check-isolation, unit, clean.
# Rationale and layout: docs/DECISIONS.md D-0014, docs/ARCHITECTURE.md §1.

CC       ?= cc
AR       ?= ar

CSTD     ?= -std=c17
WARN     ?= -Wall -Wextra -Wpedantic
OPT      ?= -O2 -g
SANFLAGS ?=

CPPFLAGS += -Iinclude/wonderland -D_POSIX_C_SOURCE=200809L
# Narrative code sees its own headers only; it cannot resolve shell headers
# even if someone writes the include (docs/ARCHITECTURE.md §3.1).
NARRATIVE_CPPFLAGS := -Iinclude/wonderland/narrative -D_POSIX_C_SOURCE=200809L
TEST_CPPFLAGS := $(CPPFLAGS) -Itests/unit

CFLAGS  += $(CSTD) $(WARN) $(OPT) $(SANFLAGS)
LDFLAGS ?=
LDLIBS  ?=

BUILD ?= build

BIN           := $(BUILD)/wonderland
SHELL_LIB     := $(BUILD)/libwl_shell.a
NARRATIVE_LIB := $(BUILD)/libwl_narrative.a
UNIT_BIN      := $(BUILD)/wl_tests
PROBE_BIN     := $(BUILD)/narrative_probe
PTY_BIN       := $(BUILD)/pty_test

SHELL_SRCS := $(wildcard \
    src/core/*.c src/term/*.c src/lex/*.c src/parse/*.c src/ast/*.c \
    src/expand/*.c src/exec/*.c src/jobs/*.c src/builtins/*.c \
    src/line/*.c src/content/*.c)
NARRATIVE_SRCS := $(wildcard src/narrative/*.c)
TEST_SRCS      := $(wildcard tests/unit/*.c)

SHELL_OBJS     := $(SHELL_SRCS:%.c=$(BUILD)/obj/%.o)
NARRATIVE_OBJS := $(NARRATIVE_SRCS:%.c=$(BUILD)/obj/%.o)
TEST_OBJS      := $(TEST_SRCS:%.c=$(BUILD)/obj/%.o)
MAIN_OBJ       := $(BUILD)/obj/src/main.o

DEPS := $(SHELL_OBJS:.o=.d) $(NARRATIVE_OBJS:.o=.d) \
        $(TEST_OBJS:.o=.d) $(MAIN_OBJ:.o=.d)

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

.PHONY: all test unit check-isolation asan clean memcheck lint

all: $(BIN)

$(BIN): $(MAIN_OBJ) $(SHELL_LIB) $(NARRATIVE_LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(MAIN_OBJ) $(SHELL_LIB) $(NARRATIVE_LIB) $(LDLIBS)

$(SHELL_LIB): $(SHELL_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $(SHELL_OBJS)

$(NARRATIVE_LIB): $(NARRATIVE_OBJS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $(NARRATIVE_OBJS)

$(UNIT_BIN): $(TEST_OBJS) $(SHELL_LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(TEST_OBJS) $(SHELL_LIB) $(LDLIBS)

# The probe links the narrative archive in full and refuses unresolved symbols:
# the mechanical proof that the world cannot reach the shell.
$(PROBE_BIN): tests/isolation/probe.c $(NARRATIVE_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ tests/isolation/probe.c \
	    -Wl,--whole-archive $(NARRATIVE_LIB) -Wl,--no-whole-archive \
	    -Wl,--no-undefined

# The pty test defines _DEFAULT_SOURCE itself (forkpty is not POSIX); it is a
# test, not shell code (docs/ARCHITECTURE.md §11).
$(PTY_BIN): tests/pty/pty_test.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) -lutil

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/src/narrative/%.o: src/narrative/%.c
	@mkdir -p $(dir $@)
	$(CC) $(NARRATIVE_CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/tests/unit/%.o: tests/unit/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

unit: $(UNIT_BIN)
	@echo "== unit tests =="
	@$(UNIT_BIN)

check-isolation: $(PROBE_BIN)
	@echo "== isolation checks =="
	@sh scripts/check-isolation.sh

test: all unit check-isolation $(PTY_BIN)
	@echo "== CLI tests =="
	@WL_BIN="$(BIN)" sh tests/run.sh
	@echo "== pty tests =="
	@$(PTY_BIN) $(BIN)

# Optional developer checks; both skip nothing silently: if the tool is
# installed, its failure is our failure.
memcheck: $(UNIT_BIN)
	valgrind --quiet --error-exitcode=1 --leak-check=full \
	    --errors-for-leak-kinds=definite ./$(UNIT_BIN)

lint:
	cppcheck --quiet --error-exitcode=1 --enable=warning,performance,portability \
	    -Iinclude/wonderland src

# Same suite under ASan/UBSan, in a separate object tree so normal builds are
# unaffected (docs/DECISIONS.md D-0014).
asan:
	$(MAKE) BUILD=build/asan \
	    SANFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
	    test

clean:
	rm -rf build

-include $(DEPS)
