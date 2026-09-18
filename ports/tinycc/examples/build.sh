# Rebuild the compiler twice, then compile and run its native checks.
cd /home/user/tinycc

kirkc tcc.c -o stage2 && ./stage2 tcc.c -o stage3
./stage3 ../hello.c -o greeting && ./greeting
./stage3 ../nativecheck.c ../companion.c -o check && ./check
echo native build complete
