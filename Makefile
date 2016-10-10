dryer: dryer.ino mock.h
	g++ -x c++ -Wall -DMOCK -o $@ $<
