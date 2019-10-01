CC              = gcc
LD              = gcc
CFLAGS          = -Wall -Werror -Og

LDFLAGS         = 
DEFS            =
LIB             =

all:    echo-server echo-client

echo-server:    echo-server.c
	$(CC) $(DEFS) $(CFLAGS) -c echo-server.c
	$(LD) -o $@ $(LDFLAGS) echo-server.o $(LIB)

echo-client:    echo-client.c
	$(CC) $(DEFS) $(CFLAGS) -c echo-client.c
	$(LD) -o $@ $(LDFLAGS) echo-client.o $(LIB)

clean:
	rm -f *.o
	rm -f echo-server
	rm -f echo-client
