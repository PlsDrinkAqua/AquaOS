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
errno/errno.o \
stdio/getchar.o \
stdio/printf.o \
stdio/putchar.o \
stdio/puts.o \
stdlib/exit.o \
stdlib/malloc.o \
string/strcmp.o \
string/strncmp.o \
string/strlen.o \
syscall/syscall.o \
unistd/chdir.o \
unistd/close.o \
unistd/dup.o \
unistd/exec.o \
unistd/fork.o \
unistd/lseek.o \
unistd/open.o \
unistd/pipe.o \
unistd/read.o \
unistd/readdir.o \
unistd/sbrk.o \
unistd/sleep.o \
unistd/spawn.o \
unistd/wait.o \
unistd/write.o \
unistd/yield.o \
zenos/readline.o \
zenos/task.o \
zenos/terminal.o

.PHONY: all clean
.SUFFIXES: .o .c

all: $(TARGET) crt0.o

$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)

crt0.o: crt0.S
	$(CC) -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

.c.o:
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS) $(CPPFLAGS)

clean:
	rm -f $(TARGET)
	rm -f $(OBJS) $(OBJS:.o=.d) *.o *.d */*.o */*.d

-include $(OBJS:.o=.d)
