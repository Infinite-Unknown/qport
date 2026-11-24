/* SPDX-License-Identifier: MIT
 *
 * The rule set. Each rule is grounded in the QNX OS 8.0 documentation:
 *   QPG  = QNX Porting Guide (QNX Everywhere docs, com.qnx.doc.qpg)
 *   LIB  = QNX OS 8.0 C Library Reference (com.qnx.doc.neutrino.lib_ref)
 *   SOCK = io-sock User's Guide, Networking APIs
 * "Not in the 8.0 C Library Reference" means the function has no entry there;
 * treat it as unavailable unless your SDP says otherwise.
 *
 * qport:ignore-file (this file is the pattern list itself)
 */
#include "qport.h"

#include <string.h>

const rule qport_rules[] = {
    /* ---------------- headers that do not exist on QNX ---------------- */
    { "QNX001", SEV_ERROR, RK_INCLUDE_PREFIX, "linux/|asm/|asm-generic/",
      "Linux kernel header; QNX has no Linux kernel headers",
      "Remove the include or wrap it in #ifdef __linux__. On QNX, device "
      "access goes through resource managers (open/read/write/devctl) instead "
      "of Linux ioctl structures.",
      "QPG: Key differences between Linux and QNX; System architecture considerations" },

    { "QNX002", SEV_ERROR, RK_INCLUDE, "sys/epoll.h",
      "epoll is Linux-only",
      "Use poll() or select(); for event-driven servers on QNX, use ionotify() "
      "to get a pulse on a channel and MsgReceivePulse() / MsgReceive().",
      "QPG: Deep component analysis (\"epoll(), QNX uses poll(), select(), or devctl()\")" },

    { "QNX003", SEV_ERROR, RK_INCLUDE, "sys/eventfd.h|sys/signalfd.h|sys/timerfd.h",
      "eventfd/signalfd/timerfd headers do not exist on QNX",
      "QNX has no direct equivalent: write a small shim (the QNX Porting Guide "
      "shows an eventfd shim), or use native mechanisms: timer_create() with "
      "SIGEV_PULSE for timers, pulses on a channel for wakeups, sigwaitinfo() "
      "for signals.",
      "QPG: Creating shim files for porting Linux system calls to QNX" },

    { "QNX004", SEV_ERROR, RK_INCLUDE, "sys/prctl.h|sys/sysinfo.h|sys/personality.h|sys/klog.h|sys/fanotify.h|sys/sysmacros.h",
      "Linux-specific system header",
      "Remove it. prctl(PR_SET_NAME) maps to pthread_setname_np(); process "
      "abilities on QNX are managed with procmgr_ability(); the CPU count "
      "is _syspage_ptr->num_cpu (<sys/syspage.h>).",
      "LIB: prctl(), sysinfo() not in the 8.0 C Library Reference" },

    { "QNX005", SEV_ERROR, RK_INCLUDE_PREFIX, "systemd/|sys/capability.h|libudev.h|linux-|gnu/libc-version.h",
      "Depends on a Linux userspace component (systemd, libcap, udev, glibc)",
      "Not part of QNX. Make the feature optional (#ifdef __linux__) or replace "
      "it: QNX services are started by a startup script or SLM, devices "
      "appear as resource managers under /dev.",
      "QPG: Deep component analysis (third-party libraries)" },

    { "QNX006", SEV_WARN, RK_INCLUDE, "execinfo.h",
      "glibc backtrace() API",
      "backtrace() is not in the QNX 8.0 C Library Reference. Wrap the crash "
      "handler in #ifdef __GLIBC__ or use QNX's own backtrace library.",
      "LIB: backtrace() not in the 8.0 C Library Reference" },

    { "QNX007", SEV_WARN, RK_INCLUDE, "elf.h",
      "QNX puts ELF definitions in <sys/elf.h>",
      "Use #if defined(__QNX__) #include <sys/elf.h> #else #include <elf.h> "
      "#endif (this is what the OpenCV port to QNX does).",
      "QPG: Porting OpenCV to QNX (system header compatibility)" },

    { "QNX008", SEV_WARN, RK_INCLUDE, "sys/ipc.h|sys/shm.h|sys/msg.h|sys/sem.h",
      "System V IPC",
      "shmget()/msgget()/semget() are not in the QNX 8.0 C Library Reference. "
      "Use POSIX shm_open()+mmap(), mq_open(), sem_open(), or QNX native "
      "message passing (MsgSend/MsgReceive/MsgReply).",
      "LIB: shm_open(), mq_open(); QPG: Interprocess communication and resource manager" },

    { "QNX009", SEV_NOTE, RK_INCLUDE, "sys/inotify.h",
      "inotify exists on QNX but lives in libfsnotify and needs fsevmgr",
      "Link with -lfsnotify and make sure the fsevmgr service runs on the "
      "target. QNX adds inotify_qnx_ext() for QNX-specific options.",
      "LIB: inotify_init() (Library: libfsnotify)" },

    { "QNX010", SEV_NOTE, RK_INCLUDE, "mqueue.h",
      "POSIX message queues need the mqueue manager on QNX",
      "mq_open() is in libc, but the message queue manager must be running "
      "on the target (see the mqueue utility).",
      "LIB: mq_open()" },

    /* ---------------- Linux-only calls ---------------- */
    { "QNX020", SEV_ERROR, RK_CALL, "epoll_create|epoll_create1|epoll_ctl|epoll_wait|epoll_pwait|epoll_pwait2",
      "epoll is Linux-only",
      "Use poll()/select(), or ionotify() with a pulse delivered to a channel "
      "and a MsgReceive() loop, which is the native QNX event model.",
      "QPG: Deep component analysis" },

    { "QNX021", SEV_ERROR, RK_CALL, "eventfd|eventfd_read|eventfd_write",
      "eventfd() does not exist on QNX",
      "Write a shim (the QNX Porting Guide links an eventfd shim), use a "
      "pipe() as a wakeup channel, or send a pulse with MsgSendPulse().",
      "QPG: Creating shim files for porting Linux system calls to QNX" },

    { "QNX022", SEV_ERROR, RK_CALL, "timerfd_create|timerfd_settime|timerfd_gettime",
      "timerfd does not exist on QNX",
      "Use timer_create() with a SIGEV_PULSE sigevent and receive the pulse "
      "with MsgReceivePulse(): deterministic periodic timers are what QNX is "
      "built for. Use CLOCK_MONOTONIC.",
      "QPG: Creating shim files; LIB: timer_create() (SIGEV_PULSE)" },

    { "QNX023", SEV_ERROR, RK_CALL, "signalfd",
      "signalfd() does not exist on QNX",
      "Block the signals and handle them in a dedicated thread with "
      "sigwaitinfo(), or turn them into pulses (SIGEV_PULSE).",
      "QPG: Creating shim files for porting Linux system calls to QNX" },

    { "QNX024", SEV_ERROR, RK_CALL, "sched_setaffinity|sched_getaffinity|pthread_setaffinity_np|pthread_getaffinity_np",
      "Linux CPU affinity API",
      "Use ThreadCtl(_NTO_TCTL_RUNMASK_GET_AND_SET_INHERIT, ...) to set the "
      "thread's runmask (and its children's).",
      "LIB: ThreadCtl() (_NTO_TCTL_RUNMASK*); sched_setaffinity() not in the 8.0 reference" },

    { "QNX025", SEV_ERROR, RK_CALL, "prctl",
      "prctl() is Linux-only",
      "PR_SET_NAME -> pthread_setname_np(0, name); capability/privilege "
      "options -> procmgr_ability().",
      "LIB: pthread_setname_np(); prctl() not in the 8.0 reference" },

    { "QNX026", SEV_ERROR, RK_CALL, "syscall",
      "Raw Linux system call numbers do not exist on QNX",
      "Call the libc function instead (e.g. gettid() exists on QNX), or the "
      "QNX kernel call (ThreadCtl, ChannelCreate, ...).",
      "LIB: gettid(); QPG: Key differences between Linux and QNX (kernel architecture)" },

    { "QNX027", SEV_ERROR, RK_CALL, "clone|clone3|unshare|setns|pivot_root",
      "Linux namespaces/clone are not available on QNX",
      "Use pthread_create() for threads and posix_spawn()/spawn() for "
      "processes. For isolation on QNX, look at security policies and QNX "
      "containers.",
      "QPG: Deep component analysis (system calls)" },

    { "QNX028", SEV_WARN, RK_CALL, "memfd_create",
      "memfd_create() is not in the QNX 8.0 C Library Reference",
      "Use shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600) for an anonymous shared "
      "memory object, then ftruncate() + mmap().",
      "LIB: shm_open() (SHM_ANON)" },

    { "QNX029", SEV_WARN, RK_CALL, "sysinfo|get_nprocs|get_nprocs_conf|get_phys_pages|get_avphys_pages",
      "glibc/Linux system information call",
      "Use sysconf() for POSIX limits and the QNX system page "
      "(_syspage_ptr->num_cpu, <sys/syspage.h>) for the CPU count.",
      "LIB: sysinfo(), get_nprocs() not in the 8.0 reference" },

    { "QNX030", SEV_WARN, RK_CALL, "getauxval",
      "getauxval() is not in the QNX 8.0 C Library Reference",
      "The OpenCV port reads the aux vector through <sys/auxv.h> with "
      "auxv64_t on QNX. For CPU features, check at build time instead.",
      "QPG: Porting OpenCV to QNX" },

    { "QNX031", SEV_WARN, RK_CALL, "sched_getcpu",
      "sched_getcpu() is not in the QNX 8.0 C Library Reference",
      "Avoid per-CPU assumptions, or pin the thread with a runmask "
      "(ThreadCtl) so the CPU is known.",
      "LIB: ThreadCtl()" },

    { "QNX032", SEV_WARN, RK_CALL, "backtrace|backtrace_symbols|backtrace_symbols_fd",
      "glibc backtrace() API",
      "Guard with #ifdef __GLIBC__; QNX ships its own backtrace library.",
      "LIB: backtrace() not in the 8.0 reference" },

    { "QNX033", SEV_WARN, RK_CALL, "vfork",
      "vfork() is not in the QNX 8.0 C Library Reference",
      "Use posix_spawn() or QNX spawn(); the porting guide recommends spawn "
      "over fork/vfork.",
      "QPG: Deep component analysis (\"spawn() is often preferred\")" },

    { "QNX034", SEV_NOTE, RK_CALL, "fork",
      "fork() works on QNX, but spawn is preferred",
      "If fork() is followed by exec*(), replace both with posix_spawn() or "
      "spawn(): cheaper on a microkernel and the porting guide's advice.",
      "QPG: Deep component analysis; LIB: fork(), spawn(), posix_spawn()" },

    { "QNX035", SEV_WARN, RK_CALL, "shmget|shmat|shmdt|shmctl|msgget|msgsnd|msgrcv|msgctl|semget|semop|semctl",
      "System V IPC call",
      "Not in the QNX 8.0 C Library Reference. Use shm_open()+mmap(), "
      "mq_open(), sem_open(), or QNX message passing.",
      "LIB: shm_open(), mq_open()" },

    { "QNX036", SEV_WARN, RK_CALL, "sendfile|splice|vmsplice|copy_file_range",
      "Linux zero-copy file call",
      "sendfile() is not in the QNX 8.0 C Library Reference; use a read()/"
      "write() loop (or mmap()).",
      "LIB: sendfile() not in the 8.0 reference" },

    { "QNX037", SEV_NOTE, RK_CALL, "inotify_init|inotify_init1|inotify_add_watch",
      "inotify needs libfsnotify and fsevmgr on QNX",
      "Link with -lfsnotify and run fsevmgr on the target.",
      "LIB: inotify_init() (Library: libfsnotify)" },

    { "QNX038", SEV_NOTE, RK_CALL, "mq_open",
      "POSIX message queues need the mqueue manager running",
      "Start the message queue manager on the target.",
      "LIB: mq_open()" },

    { "QNX039", SEV_WARN, RK_CALL, "mallinfo|mallinfo2|malloc_trim|malloc_usable_size",
      "glibc malloc introspection",
      "Guard with #ifdef __GLIBC__. QNX tunes its allocator with mallopt().",
      "LIB: mallopt()" },

    /* ---------------- macros / identifiers ---------------- */
    { "QNX050", SEV_WARN, RK_IDENT, "SO_BINDTODEVICE|TCP_CORK|MSG_MORE|TCP_QUICKACK|SO_PRIORITY|IP_FREEBIND",
      "Linux-only socket option",
      "io-sock's socket API is ported from FreeBSD, so use the FreeBSD "
      "equivalent where one exists (TCP_NOPUSH for TCP_CORK, bind to the "
      "interface address instead of SO_BINDTODEVICE, one send() instead of "
      "MSG_MORE) or drop the option.",
      "SOCK: Socket API (functions ported from FreeBSD)" },

    { "QNX052", SEV_WARN, RK_IDENT, "O_TMPFILE|O_PATH|O_NOATIME|O_DIRECT",
      "Linux-only open() flag",
      "Not among the QNX 8.0 open() flags. Use mkstemp() + unlink() for a "
      "temporary file and a plain O_RDONLY descriptor otherwise.",
      "LIB: open()" },

    { "QNX053", SEV_WARN, RK_IDENT, "MAP_POPULATE|MAP_HUGETLB|MAP_LOCKED|MAP_GROWSDOWN|MADV_DONTDUMP|MADV_HUGEPAGE",
      "Linux-only mmap()/madvise() flag",
      "Not among the QNX 8.0 mmap() flags. To keep memory resident for "
      "real-time code use mlock()/mlockall(); QNX-specific flags include "
      "MAP_LAZY and MAP_PHYS.",
      "LIB: mmap()" },

    { "QNX054", SEV_WARN, RK_IDENT, "CLOCK_BOOTTIME|CLOCK_MONOTONIC_RAW|CLOCK_MONOTONIC_COARSE|CLOCK_REALTIME_COARSE|CLOCK_TAI",
      "Linux-only clock id",
      "QNX 8.0 clock_gettime() documents CLOCK_REALTIME, CLOCK_MONOTONIC and "
      "the CPU-time clocks. Use CLOCK_MONOTONIC for intervals.",
      "LIB: clock_gettime()" },

    { "QNX051", SEV_NOTE, RK_DEFINE, "_GNU_SOURCE",
      "_GNU_SOURCE is a glibc feature macro",
      "On QNX, define _QNX_SOURCE (or _POSIX_C_SOURCE / _XOPEN_SOURCE) to "
      "expose the full API set.",
      "QPG: Deep component analysis (macro conflicts)" },

    /* ---------------- paths ---------------- */
    { "QNX060", SEV_ERROR, RK_STRING_PREFIX, "/sys/",
      "sysfs path; QNX has no /sys",
      "Read hardware state from the driver's resource manager (devctl() or "
      "read() on its /dev path) or from the system page.",
      "QPG: Key differences between Linux and QNX (resource management)" },

    { "QNX061", SEV_WARN, RK_STRING_PREFIX, "/proc/cpuinfo|/proc/meminfo|/proc/stat|/proc/loadavg|/proc/self/status|/proc/self/maps|/proc/net/|/proc/sys/",
      "Linux /proc file",
      "QNX's /proc is a process filesystem with a different layout and no "
      "Linux text files. Use sysconf() for limits and the system page "
      "(_syspage_ptr) for CPU and memory layout.",
      "QPG: Key differences between Linux and QNX" },

    { "QNX062", SEV_NOTE, RK_STRING_PREFIX, "/dev/shm/",
      "Path into /dev/shm",
      "On QNX, use shm_open() with a name; the shared memory namespace is "
      "/dev/shmem.",
      "LIB: shm_open()" },

    /* ---------------- build files ---------------- */
    { "QNX070", SEV_ERROR, RK_BUILD_TEXT, "-fsplit-stack",
      "-fsplit-stack is not supported by qcc",
      "Remove it, and set a fixed stack size with -Wl,-z,stack-size,<bytes>.",
      "QPG: Some common troubles when cross compiling (stack size)" },

    { "QNX071", SEV_NOTE, RK_BUILD_TEXT, "-ldl|-lrt",
      "Linux link library that QNX keeps in libc",
      "dlopen(), clock_gettime(), timer_create() and mq_open() are in libc on "
      "QNX 8.0; drop the extra library if the link fails.",
      "LIB: dlopen(), clock_gettime(), mq_open() (Library: libc)" },

    { "QNX072", SEV_NOTE, RK_SPECIAL, "cmake-linux",
      "Linux-only branch in CMake; a QNX build takes the other path",
      "Add an if(QNX) / CMAKE_SYSTEM_NAME STREQUAL \"QNX\" branch (QNX "
      "toolchain files set QNX TRUE).",
      "QPG: Porting source code mechanics (build system adaptations)" },

    { "QNX073", SEV_WARN, RK_SPECIAL, "host-uname",
      "Platform picked with uname, which reports the build host",
      "When cross compiling with qcc, uname describes the Linux or Windows "
      "host, not the QNX target. Decide on the target instead: CMake "
      "CMAKE_SYSTEM_NAME / if(QNX), or a TARGET_OS make variable.",
      "QPG: Native compiler vs cross compiler" },

    /* ---------------- dedicated checks ---------------- */
    { "QNX080", SEV_WARN, RK_SPECIAL, "link-socket",
      "Uses sockets, but the build never links libsocket",
      "On QNX, socket functions need -lsocket (CMake: "
      "target_link_libraries(<t> socket) inside if(QNX)).",
      "QPG: Make (\"If the code uses regular expressions or sockets ... LDLIBS\"); QPG: CMake example (-lsocket)" },

    { "QNX081", SEV_WARN, RK_SPECIAL, "link-regex",
      "Uses POSIX regex, but the build never links libregex",
      "On QNX, regcomp()/regexec() live in libregex: add -lregex.",
      "LIB: regcomp() (Library: libregex)" },

    { "QNX082", SEV_WARN, RK_SPECIAL, "link-fsnotify",
      "Uses inotify, but the build never links libfsnotify",
      "Add -lfsnotify.",
      "LIB: inotify_init() (Library: libfsnotify)" },

    { "QNX083", SEV_WARN, RK_SPECIAL, "stack-array",
      "Large array on the stack",
      "qcc produces binaries with a much smaller default stack than Linux's "
      "8 MB. Move the buffer to the heap or a static, or link with "
      "-Wl,-z,stack-size,<bytes> (and set pthread_attr_setstacksize() for "
      "threads).",
      "QPG: Some common troubles when cross compiling (process stack size)" },

    { "QNX084", SEV_NOTE, RK_SPECIAL, "vla",
      "Variable-length array on the stack",
      "Size is decided at run time and the QNX default stack is small; cap "
      "it or allocate on the heap.",
      "QPG: Some common troubles when cross compiling (VLA)" },

    { "QNX085", SEV_NOTE, RK_SPECIAL, "open-directory",
      "Directory file descriptor opened without O_DIRECTORY",
      "On QNX you must pass O_DIRECTORY when opening a directory with open().",
      "QPG: Some common troubles when cross compiling (file APIs)" },

    { "QNX090", SEV_WARN, RK_SPECIAL, "sysroot-missing",
      "Header not found in the QNX 8.0 target sysroot",
      "The header is neither in $QNX_TARGET nor in this project. Port or "
      "cross-build the library that provides it (check the QNX open-source "
      "ports dashboard first), or guard the include.",
      "QPG: Deep component analysis (third-party libraries)" },
};

const size_t qport_rule_count = sizeof qport_rules / sizeof qport_rules[0];

const rule *rule_by_id(const char *id)
{
    for (size_t i = 0; i < qport_rule_count; i++)
        if (strcmp(qport_rules[i].id, id) == 0)
            return &qport_rules[i];
    return NULL;
}

const char *sev_name(severity s)
{
    switch (s) {
    case SEV_ERROR: return "error";
    case SEV_WARN:  return "warning";
    default:        return "note";
    }
}
