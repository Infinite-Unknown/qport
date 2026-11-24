// Nothing in this file should be reported.
// epoll_wait(ep, events, 8, -1) inside a comment
/* eventfd(0, 0); timerfd_create(); #include <sys/epoll.h> */
#include <string>

static const char *msg = "call epoll_wait() and read /proc/cpuinfo later";
static const char *raw = R"(epoll_create1(0) "/sys/class" )";

struct Loop {
    int epoll_wait(int);
    int syscall;
};

int eventfd(unsigned int initval, int flags);          // our own shim declaration
static inline int timerfd_create(int c, int f) { return -1; } // shim definition

int use(Loop &l, Loop *p)
{
    l.epoll_wait(3);            // member call
    p->epoll_wait(4);           // member call through pointer
    int n = p->syscall;         // member access
    std::string s = "prctl(";   // string
    char c = '/';
    return n + (int)s.size() + c;
}

namespace mine { int fork(); }
int other() { return mine::fork(); }   // qualified, not the libc fork
