/* telemetryd: samples vehicle sensors every 10 ms and publishes them over UDP. */
#define _GNU_SOURCE
#include <errno.h>
#include <execinfo.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/timerfd.h>

#ifdef __linux__
#include <linux/watchdog.h>
#include <sys/ioctl.h>
#endif

#include "telemetryd.h"

static int stop_fd = -1;

static void on_crash(int sig)
{
    void *frames[32];
    int n = backtrace(frames, 32);
    backtrace_symbols_fd(frames, n, 2);
    _exit(128 + sig);
}

static void on_term(int sig)
{
    (void)sig;
    eventfd_write(stop_fd, 1);
}

static void pin_to_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof set, &set) != 0)
        perror("sched_setaffinity");
}

static int start_tick(int period_ms)
{
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    struct itimerspec its = {
        .it_interval = { 0, period_ms * 1000000L },
        .it_value    = { 0, period_ms * 1000000L },
    };
    timerfd_settime(fd, 0, &its, NULL);
    return fd;
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "192.168.1.10";
    const char *cfg = "/etc/telemetryd.conf";

    signal(SIGSEGV, on_crash);
    signal(SIGTERM, on_term);
    prctl(PR_SET_NAME, "telemetryd", 0, 0, 0);
    pin_to_cpu(2);

    stop_fd = eventfd(0, EFD_NONBLOCK);
    int tick = start_tick(10);
    int sens = sensors_open();
    int sock = publish_open(host, "5005");
    int watch = config_watch(cfg);

#ifdef __linux__
    int wd = open("/dev/watchdog", O_WRONLY);
    int timeout = 1;
    ioctl(wd, WDIOC_SETTIMEOUT, &timeout);
#endif

    int ep = epoll_create1(0);
    struct epoll_event ev = { .events = EPOLLIN };
    ev.data.fd = tick;  epoll_ctl(ep, EPOLL_CTL_ADD, tick, &ev);
    ev.data.fd = stop_fd; epoll_ctl(ep, EPOLL_CTL_ADD, stop_fd, &ev);
    ev.data.fd = watch; epoll_ctl(ep, EPOLL_CTL_ADD, watch, &ev);

    /* one second of samples, batched before each publish */
    struct sample batch[100 * MAX_SIGNALS * 4];
    int fill = 0;

    for (;;) {
        struct epoll_event events[8];
        int n = epoll_wait(ep, events, 8, -1);
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == stop_fd)
                goto out;
            if (fd == watch) {
                config_reload(cfg);
                continue;
            }
            uint64_t expirations;
            read(tick, &expirations, sizeof expirations);
            fill += sensors_read(batch + fill, MAX_SIGNALS);
            if (fill >= 100 * MAX_SIGNALS) {
                publish_batch(sock, batch, fill);
                fill = 0;
            }
#ifdef __linux__
            write(wd, "k", 1);
#endif
        }
    }
out:
    (void)sens;
    return 0;
}
