#!/bin/bash
set -e

IMG=ext2_hda.img
MNT=/mnt/ext2_test
SHELL_PROGRAM=./user/shell
HELLO=./user/hello
MEMTEST=./user/memtest
SLEEPTEST=./user/sleeptest
YIELDTEST=./user/yieldtest
FAULTTEST=./user/faulttest
TASKTEST=./user/tasktest
FDTEST=./user/fdtest
UNIXTEST=./user/unixtest

. ./config.sh
make -C user

# 1. 先创建一个基础镜像
dd if=/dev/zero of="$IMG" bs=1M count=100        # 100 MiB 空盘
mkfs.ext2 -F "$IMG"                              # 在整个文件上格式化 ext2（-F 允许格式化文件）

# 2. 挂载镜像并创建测试文件
sudo mkdir -p "$MNT"
sudo mount -o loop "$IMG" "$MNT"

# 在镜像里创建文件和子目录
echo "hello, ZenOS" | sudo tee "$MNT/hello.txt" > /dev/null
sudo mkdir -p "$MNT/subdir"
echo "foo" | sudo tee "$MNT/subdir/bar.dat" > /dev/null

sudo cp "$SHELL_PROGRAM" "$MNT/shell"
sudo cp "$HELLO" "$MNT/hello"
sudo cp "$MEMTEST" "$MNT/memtest"
sudo cp "$SLEEPTEST" "$MNT/sleeptest"
sudo cp "$YIELDTEST" "$MNT/yieldtest"
sudo cp "$FAULTTEST" "$MNT/faulttest"
sudo cp "$TASKTEST" "$MNT/tasktest"
sudo cp "$FDTEST" "$MNT/fdtest"
sudo cp "$UNIXTEST" "$MNT/unixtest"

for program in cat ls echo argtest; do
    sudo cp "./user/$program" "$MNT/$program"
done

# 确保写入磁盘
sync

# 卸载并清理挂载点
sudo umount "$MNT"
sudo rmdir "$MNT"

# 3. 复制到 disk 目录
mkdir -p disk
cp "./$IMG" disk/"$IMG"

echo "Generation completed: $IMG with test files created"
