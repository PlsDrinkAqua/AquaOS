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

TARGETS = shell hello memtest yieldtest

.PHONY: all clean userlibc
.SUFFIXES: .o .c .S

all: $(TARGETS)

userlibc:
	$(MAKE) -C ../userlibc

shell: shell.o userlibc user.ld
	$(CC) -T user.ld -o $@ $(CFLAGS) $(LDFLAGS) shell.o $(LIBS)

hello: hello.o userlibc user.ld
	$(CC) -T user.ld -o $@ $(CFLAGS) $(LDFLAGS) hello.o $(LIBS)

memtest: memtest.o userlibc user.ld
	$(CC) -T user.ld -o $@ $(CFLAGS) $(LDFLAGS) memtest.o $(LIBS)

yieldtest: yieldtest.o userlibc user.ld
	$(CC) -T user.ld -o $@ $(CFLAGS) $(LDFLAGS) yieldtest.o $(LIBS)

.c.o:
	$(CC) -MD -c $< -o $@ -std=gnu11 $(CFLAGS) $(CPPFLAGS)

.S.o:
	$(CC) -MD -c $< -o $@ $(CFLAGS) $(CPPFLAGS)

clean:
	rm -f $(TARGETS)
	rm -f shell.o hello.o memtest.o yieldtest.o *.d
	$(MAKE) -C ../userlibc clean

-include shell.d hello.d memtest.d yieldtest.d
