# CMPU-375 Simple IRC server makefile
#
# Please change as needed.
#

CC			= gcc
LD      = gcc


# compilation flag explanation:
#  -Wall		show all warnings
#  -Werror	treat warnings as errors (stop compilation)
#  -g 			generate debug information to be used by gdb
#  -DDEBUG	defines a macro DEBUG to enable the printing of debug messages
#          	using the macros in debug.h
CFLAGS	= -Wall -Werror -g -DDEBUG
LDFLAGS =
DEFS 		=

all: sircs


sircs: sircs.c sircs.h irc-proto.o debug.o linked-list.o
	$(CC) $(DEFS) $(CFLAGS) -c sircs.c
	$(LD) -o $@ $(LDFLAGS) sircs.o irc-proto.o debug.o linked-list.o $(LIB)

debug.o: debug-text.h debug.c debug.h
	$(CC) $(DEFS) $(CFLAGS) -c debug.c

irc-proto.o: irc-proto.c irc-proto.h
	$(CC) $(DEFS) $(CFLAGS) -c irc-proto.c

linked-list.o: linked-list.c linked-list.h
	$(CC) $(DEFS) $(CFLAGS) -c linked-list.c

debug-text.h: debug.h
	./dbparse.pl < debug.h > debug-text.h

clean:
	rm -f *.o sircs

test:
	./sircs-tester.rb
