CC=g++
all: release

release:
	mkdir -p bin
	$(CC) main.cpp -o bin/check_gluster_perf

debug:
	mkdir -p bin
	$(CC) -g main.cpp -o bin/check_gluster_perf