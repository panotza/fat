CC=gcc
FILE?=TEST    TXT

.PHONY: image debug

fat: fat.c
	$(CC) -Wall -Wextra -pedantic -o fat fat.c

run: fat
	./fat test.img "$(FILE)"

image:
	dd if=/dev/zero of=test.img bs=512 count=2880 && \
	mkfs.fat -F 12 -n "NBOS" test.img && \
	mcopy -i test.img test.txt "::test.txt"

debug:
	xxd -g 1 test.img > debug.txt