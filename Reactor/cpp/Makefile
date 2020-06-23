SOURCES = $(wildcard *.cpp)
OBJECTS = $(patsubst %.cpp, %.o, $(SOURCES))

CC = g++
CFLAGS = -Wall -g -O2
LIBRARY = -lpthread

TARGET = reactor

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBRARY)

.cpp.o:
	$(CC) $(CFLAGS) -c -o $@ $< $(LIBRARY)

.PHONY: clean
clean:
	rm -f $(TARGET) *.o
