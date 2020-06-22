SOURCES = $(wildcard *.c)
OBJECTS = $(patsubst %.c, %.o, $(SOURCES))

CC = gcc
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
