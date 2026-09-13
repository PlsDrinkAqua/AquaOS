DEFAULT_HOST!=../default-host.sh
HOST?=$(DEFAULT_HOST)
HOSTARCH!=../target-triplet-to-arch.sh $(HOST)

CFLAGS?=-O2 -g
CPPFLAGS?=
LDFLAGS?=
LIBS?=
ifeq ($(origin CC),default)
CC=$(HOST)-gcc
endif

CFLAGS:=$(CFLAGS) -ffreestanding -fno-builtin -Wall -Wextra
CPPFLAGS:=$(CPPFLAGS) -I../userlibc/include
LDFLAGS:=$(LDFLAGS)
LIBS:=$(LIBS) ../userlibc/libc.a -nostdlib -lgcc

# 用户程序不需要 kernel 专用宏
# 不要加 -D__is_kernel
# 不要链接 -lk

TARGETS = shell hello cat ls echo mkdir rm rmdir

.PHONY: all clean userlibc
.SUFFIXES: .o .c .S

all: $(TARGETS)

userlibc:
	$(MAKE) -C ../userlibc

$(TARGETS) fscheck: %: %.o userlibc user.ld
	$(CC) -T user.ld -o $@ $(CFLAGS) $(LDFLAGS) ../userlibc/crt0.o $< $(LIBS)

fscheck.o: ../tests/fs_delete.c
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS) $(CPPFLAGS)

.c.o:
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS) $(CPPFLAGS)

.S.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

clean:
	rm -f $(TARGETS) fscheck fscheck.o
	rm -f $(TARGETS:=.o) *.d
	$(MAKE) -C ../userlibc clean

-include $(TARGETS:=.d) fscheck.d
