DEFAULT_HOST!=../default-host.sh
HOST?=$(DEFAULT_HOST)
HOSTARCH!=../target-triplet-to-arch.sh $(HOST)

CFLAGS?=-O2 -g
CPPFLAGS?=
ifeq ($(origin AR),default)
AR=$(HOST)-ar
endif
ifeq ($(origin CC),default)
CC=$(HOST)-gcc
endif

CFLAGS:=$(CFLAGS) -ffreestanding -fno-builtin -Wall -Wextra
CPPFLAGS:=$(CPPFLAGS) -Iinclude

TARGET=libc.a

OBJS=\
stdio/getchar.o \
stdio/printf.o \
stdio/putchar.o \
stdio/puts.o \
stdlib/exit.o \
string/strcmp.o \
string/strncmp.o \
string/strlen.o \
syscall/syscall.o \
unistd/exec.o \
unistd/read.o \
unistd/spawn.o \
unistd/wait.o \
unistd/write.o \
zenos/readline.o \
zenos/task.o \
zenos/terminal.o

.PHONY: all clean
.SUFFIXES: .o .c

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)

.c.o:
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS) $(CPPFLAGS)

clean:
	rm -f $(TARGET)
	rm -f $(OBJS) $(OBJS:.o=.d) *.o *.d */*.o */*.d

-include $(OBJS:.o=.d)
