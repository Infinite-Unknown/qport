#include <stdio.h>

#ifdef __linux__
#include <sys/epoll.h>            /* guarded: QNX never compiles this */
static int a(void) { return epoll_create1(0); }
#endif

#if defined(__QNX__)
#include <sys/neutrino.h>
#else
#include <sys/timerfd.h>          /* guarded: the #else of a QNX test */
#endif

#ifndef __QNX__
static int b(void) { return eventfd(0, 0); }   /* guarded */
#endif

#if 0
static int c(void) { return signalfd(-1, 0, 0); } /* guarded: dead code */
#endif

#if defined(__linux__) || defined(__QNX__)
static int d(void) { return prctl(1, 0, 0, 0, 0); } /* NOT guarded: QNX compiles this */
#endif

#if defined(__APPLE__)
static int e(void) { return 1; }
#elif defined(__linux__)
static int f(void) { return sched_setaffinity(0, 0, 0); } /* guarded */
#else
static int g(void) { return memfd_create("x", 0); }       /* NOT guarded */
#endif

#if __GLIBC__ >= 2
static void h(void) { backtrace(0, 0); }                    /* guarded: no glibc on QNX */
#endif

#if HAVE_SOMETHING
static int i(void) { return timerfd_create(1, 0); }         /* NOT guarded: unknown macro */
#endif
