#include <sys/epoll.h> // qport:ignore
int a(void) { return epoll_create1(0); } /* qport:ignore QNX020 */
int b(void) { return eventfd(0, 0); }    /* qport:ignore QNX020 (wrong id: still reported) */
