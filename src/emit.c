/* SPDX-License-Identifier: MIT
 *
 * Writes QNX OS 8.0 build configuration for a scanned project: a CMake
 * toolchain file and a make fragment, following the QNX Porting Guide's
 * CMake and Make examples, with the extra link libraries the scan found.
 */
#include "qport.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void libs(const build_facts *f, strbuf *out)
{
    sb_puts(out, "");
    if (f->uses_sockets)
        sb_puts(out, " -lsocket");
    if (f->uses_regex)
        sb_puts(out, " -lregex");
    if (f->uses_inotify)
        sb_puts(out, " -lfsnotify");
}

static int write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "qport: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    fputs(text, fp);
    if (fclose(fp) != 0) {
        fprintf(stderr, "qport: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

int emit_toolchain(const char *dir, const char *arch, const build_facts *f)
{
    const char *proc;
    if (!strcmp(arch, "aarch64le"))
        proc = "aarch64";
    else if (!strcmp(arch, "x86_64"))
        proc = "x86_64";
    else {
        fprintf(stderr, "qport: unknown QNX 8.0 architecture '%s' (use aarch64le or x86_64)\n", arch);
        return -1;
    }
    /* gcc_ntoaarch64le / gcc_ntox86_64: the qcc -V target names */
    char vtarget[64];
    snprintf(vtarget, sizeof vtarget, "gcc_nto%s", arch);

    strbuf lb;
    sb_init(&lb);
    libs(f, &lb);
    const char *extra = lb.data[0] ? lb.data + 1 : "";

    strbuf cm;
    sb_init(&cm);
    sb_printf(&cm,
        "# QNX OS 8.0 CMake toolchain file (%s), written by qport %s.\n"
        "#\n"
        "#   source ~/qnx800/qnxsdp-env.sh\n"
        "#   cmake -B build-qnx-%s -DCMAKE_TOOLCHAIN_FILE=%s/qnx-%s.cmake\n"
        "#   cmake --build build-qnx-%s\n"
        "\n"
        "set(QNX TRUE)\n"
        "set(CMAKE_SYSTEM_NAME QNX)\n"
        "set(CMAKE_SYSTEM_PROCESSOR %s)\n"
        "\n"
        "set(QNX_HOST \"$ENV{QNX_HOST}\")\n"
        "set(QNX_TARGET \"$ENV{QNX_TARGET}\")\n"
        "if(NOT QNX_HOST OR NOT QNX_TARGET)\n"
        "  message(FATAL_ERROR \"QNX_HOST / QNX_TARGET are not set: source qnxsdp-env.sh first\")\n"
        "endif()\n"
        "\n"
        "set(CMAKE_C_COMPILER \"${QNX_HOST}/usr/bin/qcc\")\n"
        "set(CMAKE_CXX_COMPILER \"${QNX_HOST}/usr/bin/q++\")\n"
        "set(CMAKE_C_COMPILER_TARGET %s)\n"
        "set(CMAKE_CXX_COMPILER_TARGET %s)\n"
        "set(CMAKE_SYSROOT \"${QNX_TARGET}\")\n"
        "set(CMAKE_FIND_ROOT_PATH \"${QNX_TARGET}\" \"${QNX_TARGET}/%s\")\n"
        "set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)\n"
        "set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)\n"
        "set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)\n"
        "\n"
        "# Expose the full QNX API set (QNX Porting Guide: macro conflicts).\n"
        "set(CMAKE_C_FLAGS_INIT \"-D_QNX_SOURCE\")\n"
        "set(CMAKE_CXX_FLAGS_INIT \"-D_QNX_SOURCE\")\n",
        arch, QPORT_VERSION, arch, dir, arch, arch, proc, vtarget, vtarget, arch);
    if (extra[0])
        sb_printf(&cm,
            "\n# Libraries outside libc that this project needs on QNX (found by qport).\n"
            "set(CMAKE_EXE_LINKER_FLAGS_INIT \"%s\")\n"
            "set(CMAKE_SHARED_LINKER_FLAGS_INIT \"%s\")\n", extra, extra);
    sb_puts(&cm,
        "\n# qcc's default stack is far smaller than Linux's 8 MB. If qport reported\n"
        "# QNX083 (large stack arrays), either fix the code or uncomment:\n"
        "# string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT \" -Wl,-z,stack-size,8388608\")\n");

    strbuf mk;
    sb_init(&mk);
    sb_printf(&mk,
        "# QNX OS 8.0 make settings (%s), written by qport %s.\n"
        "#\n"
        "#   source ~/qnx800/qnxsdp-env.sh\n"
        "#   make -f Makefile -f %s/qnx-%s.mk\n"
        "#\n"
        "# qcc/q++ pass the QNX sysroot themselves, so no -I or -L is needed.\n"
        "\n"
        "CC  = qcc\n"
        "CXX = q++\n"
        "TARGET_ARCH = -V%s\n"
        "CPPFLAGS += -D_QNX_SOURCE\n",
        arch, QPORT_VERSION, dir, arch, vtarget);
    if (extra[0])
        sb_printf(&mk, "LDLIBS += %s\n", extra);
    sb_puts(&mk, "# LDFLAGS += -Wl,-z,stack-size,8388608\n");

    char path[4096];
    int rc = 0;
    snprintf(path, sizeof path, "%s/qnx-%s.cmake", dir, arch);
    if (write_text_file(path, cm.data) == 0)
        printf("wrote %s\n", path);
    else
        rc = -1;
    snprintf(path, sizeof path, "%s/qnx-%s.mk", dir, arch);
    if (write_text_file(path, mk.data) == 0)
        printf("wrote %s\n", path);
    else
        rc = -1;
    sb_free(&cm);
    sb_free(&mk);
    sb_free(&lb);
    return rc;
}
