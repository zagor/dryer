dryer: dryer.ino mock.h
	g++ -x c++ -Wall -Wextra -Wpedantic -DMOCK -o $@ $<
