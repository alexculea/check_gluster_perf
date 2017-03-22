CC=g++
all: release

release:
	$(CC) main.cpp -o bin/check_gluster_perf

debug:
	$(CC) -g main.cpp -o bin/check_gluster_perf