#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stdio.h>  /* for perror */

#define eprintf(fmt, args...) fprintf(stderr, fmt, ##args)

#ifdef DEBUG
extern unsigned int debug;
#define DEBUG_PRINTF(level, fmt, args...) \
        do { if (debug & (level)) fprintf(stderr, fmt , ##args ); } while(0)

#define DEBUG_VPRINTF(level, fmt, va_args) \
        do { if (debug & (level)) vfprintf(stderr, fmt , va_args ); } while(0)

#define DEBUG_PERROR(errmsg) \
        do { if (debug & DEBUG_ERRS) perror(errmsg); } while(0)
#else
#define DPRINTF(args...)
#define DEBUG_PERROR(args...)
#endif

/*
 * The format of this should be obvious.  Please add some explanatory
 * text if you add a debugging value.  This text will show up in
 * -d list
 */
#define DEBUG_NONE      0x00	// DBTEXT:  No debugging
#define DEBUG_ERRS      0x01	// DBTEXT:  Verbose error reporting
#define DEBUG_INIT      0x02	// DBTEXT:  Debug initialization
#define DEBUG_SOCKETS   0x04  // DBTEXT:  Debug socket operations
#define DEBUG_INPUT     0x08  // DBTEXT:  Debug client input
#define DEBUG_CLIENTS   0x10  // DBTEXT:  Debug client arrival/depart
#define DEBUG_COMMANDS  0x20  // DBTEXT:  Debug client commands
#define DEBUG_CHANNELS  0x40  // DBTEXT:  Debug channel operations
#define DEBUG_REPLIES   0x80  // DBTEXT:  Debug server replies

#define DEBUG_ALL  0xffffffff

int set_debug(char *arg);  /* Returns 0 on success, -1 on failure */
void print_hex(int level, char* str, int max);

// Fake boolean values
#define TRUE 1
#define FALSE 0
// MIN and MAX
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#endif /* _DEBUG_H_ */
