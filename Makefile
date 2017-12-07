all:fs.o
fs.o:disk.c fs.c
	gcc -c disk.c fs.c
