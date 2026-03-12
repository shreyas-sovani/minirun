# MiniRun — Minimal Container Runtime
# Makefile
#
# Targets:
#   make           — build the minirun binary
#   make clean     — remove build artifacts
#   make help      — show this message

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g -O2 -Wno-gnu-zero-variadic-macro-arguments
LDFLAGS =

# Source and output
SRCDIR  = src
SRCS    = $(SRCDIR)/main.c      \
          $(SRCDIR)/container.c \
          $(SRCDIR)/cgroup.c    \
          $(SRCDIR)/fs.c        \
          $(SRCDIR)/utils.c
OBJS    = $(SRCS:.c=.o)
TARGET  = minirun

.PHONY: all clean help

all: $(TARGET)
	@echo ""
	@echo "  ✓ Build successful → ./$(TARGET)"
	@echo "  Next: bash setup_rootfs.sh  (create rootfs)"
	@echo "        sudo ./$(TARGET) --help"
	@echo ""

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
	@echo "  ✓ Cleaned build artifacts"

help:
	@echo "Usage: make [target]"
	@echo "  all     Build the minirun binary (default)"
	@echo "  clean   Remove object files and binary"
	@echo "  help    Show this message"
