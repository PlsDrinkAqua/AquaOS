.PHONY: all iso compiler disk test clean
all:
	sh build.sh
iso: all
	python3 tools/iso64.py
compiler: all
	python3 ports/tinycc/build.py
disk: compiler
	python3 tools/disk64.py
test: compiler iso
	python3 tests/shell_smoke.py --block-size 4096 --tinycc
clean:
	sh clean.sh
