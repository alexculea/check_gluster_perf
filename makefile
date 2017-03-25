CC=g++
all: release

release:
	mkdir -p bin
	$(CC) -O main.cpp -o bin/check_gluster_perf

static:
	mkdir -p bin
	$(CC) -O main.cpp -o bin/check_gluster_perf -static-libstdc++

debug:
	mkdir -p bin
	$(CC) -O -g main.cpp -o bin/check_gluster_perf