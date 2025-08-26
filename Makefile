# ============================================
# Hand-written Makefile for z80e (no CMake)
# ============================================

# Compiler & flags
CC      := nspire-gcc
CFLAGS  := -Wall -Wextra -pedantic -O2 -fPIC \
           -Ilibz80e/include -Ilibz80e/include/z80e \
           -Iscas/common
CFLAGS  += $(if $(SCAS_INCLUDES),-I$(SCAS_INCLUDES))
ld      := nspire-ld
LDFLAGS :=
LIBS    :=
LIBS    += $(if $(SCAS_LIBRARIES),$(SCAS_LIBRARIES))

# ---- Sources ----

# Core emulator sources (from libz80e/CMakeLists.txt)
Z80E_SRC := \
    libz80e/src/core/cpu.c \
    libz80e/src/core/registers.c \
    libz80e/src/debugger/debugger.c \
    libz80e/src/debugger/hooks.c \
    libz80e/src/disassembler/disassemble.c \
    libz80e/src/ti/asic.c \
    libz80e/src/ti/memory.c \
    libz80e/src/log/log.c \
    libz80e/src/runloop/runloop.c \
    $(wildcard libz80e/src/ti/hardware/*.c) \
    $(wildcard libz80e/src/debugger/commands/*.c) \
    $(wildcard scas/common/*.c)

Z80E_OBJ := $(Z80E_SRC:.c=.o)


# TUI frontend sources (from frontends/tui/CMakeLists.txt)
TUI_SRC := \
    frontends/CX-II/main.c

TUI_OBJ := $(TUI_SRC:.c=.o)

# TUI executable
TUI_BIN := z80e-CX-II

# ============================================
# Rules
# ============================================

all: $(TUI_BIN)

# Executable (links objects + shared lib)
$(TUI_BIN):  $(TUI_OBJ) $(Z80E_OBJ)
	$(CC) -static -o $@ $(TUI_OBJ) $(Z80E_OBJ) /root/scas/bin/scas.a

# Generic compile rule
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(Z80E_OBJ) $(TUI_OBJ) $(TUI_BIN)

.PHONY: all clean
