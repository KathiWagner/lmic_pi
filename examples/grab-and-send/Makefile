CFLAGS=-I../../lmic
LDFLAGS=-lwiringPi

grab-and-send: grab-and-send.cpp
	cd ../../lmic && $(MAKE)
	$(CC) $(CFLAGS) -o grab-and-send grab-and-send.cpp ../../lmic/*.o $(LDFLAGS)
	
grab-and-send-otaa: grab-and-send-otaa.cpp
	cd ../../lmic && $(MAKE)
	$(CC) $(CFLAGS) -o grab-and-send-otaa grab-and-send-otaa.cpp ../../lmic/*.o $(LDFLAGS)

all: grab-and-send grab-and-send-otaa

.PHONY: clean

clean:
	rm -f *.o grab-and-send
	rm -f *.o grab-and-send-otaa
