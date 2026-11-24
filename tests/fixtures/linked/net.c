#include <sys/socket.h>
#include <netdb.h>
int open_sock(void) { return socket(2, 2, 0); }
