# linux-telemetryd (example input)

A small Linux daemon used as qport's demo and test input. It is written the
way a lot of embedded Linux code is: epoll + timerfd + eventfd, CPU pinning
with sched_setaffinity, sysfs and /proc reads, System V shared memory,
inotify, POSIX regex and UDP sockets.

It targets Linux only and is not built by this repository. Scan it with:

    ./qport scan examples/linux-telemetryd
