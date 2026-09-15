#!/bin/bash
set -e

IMG=ext2_hda.img
MNT=/mnt/ext2_test
SHELL_PROGRAM=./user/shell
HELLO=./user/hello

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

sudo mkdir -p "$MNT/bin" "$MNT/usr/bin" "$MNT/usr/include" "$MNT/usr/lib" "$MNT/home/user" "$MNT/tmp" "$MNT/etc"
sudo cp "$SHELL_PROGRAM" "$MNT/bin/shell"
sudo cp "$HELLO" "$MNT/bin/hello"

for program in cat ls echo mkdir rm rmdir meinkampf; do
    sudo cp "./user/$program" "$MNT/bin/$program"
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
