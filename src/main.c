/* SPDX-License-Identifier: MIT */
#include "qport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void usage(FILE *fp)
{
    fputs(
        "qport " QPORT_VERSION ": find what stops Linux C/C++ code from building or\n"
        "behaving the same on QNX OS 8.0, and what to use instead.\n"
        "\n"
        "usage:\n"
        "  qport scan [options] <path>        scan a source tree or one file\n"
        "  qport toolchain [options] <path>   scan, then write QNX 8.0 CMake/make config\n"
        "  qport rules                        list the rules\n"
        "  qport explain <RULE>               show one rule and its source\n"
        "\n"
        "scan options:\n"
        "  -f, --format text|brief|json|sarif|md|html   output format (default text)\n"
        "      --brief                        one line per finding (same as -f brief)\n"
        "  -o, --output FILE                  write the report to FILE\n"
        "      --sysroot DIR                  QNX target dir for the header check\n"
        "                                     (default: $QNX_TARGET when set)\n"
        "      --no-sysroot                   skip the header check\n"
        "      --stack-threshold BYTES        flag local arrays this big (default 65536)\n"
        "      --show-guarded                 also show code inside #if branches that a\n"
        "                                     QNX build does not compile\n"
        "      --exclude NAME                 skip directories with this name (repeatable)\n"
        "      --fail-on error|warning|note|never   exit 1 at this level (default error)\n"
        "      --color, --no-color            force colour on or off\n"
        "\n"
        "toolchain options:\n"
        "      --arch aarch64le|x86_64        QNX 8.0 target CPU (default aarch64le)\n"
        "      --out DIR                      where to write the files (default .)\n"
        "\n"
        "exit status: 0 clean, 1 findings at --fail-on level, 2 usage or I/O error\n",
        fp);
}

static int need_arg(int i, int argc, const char *opt)
{
    if (i + 1 >= argc) {
        fprintf(stderr, "qport: %s needs a value\n", opt);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    const char *cmd = argv[1];
    if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) {
        usage(stdout);
        return 0;
    }
    if (!strcmp(cmd, "-V") || !strcmp(cmd, "--version") || !strcmp(cmd, "version")) {
        printf("qport %s (rules for QNX OS 8.0, %zu rules)\n", QPORT_VERSION, qport_rule_count);
        return 0;
    }

    int color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");

    if (!strcmp(cmd, "rules")) {
        strbuf out;
        sb_init(&out);
        report_rules(color, &out);
        fputs(out.data, stdout);
        sb_free(&out);
        return 0;
    }
    if (!strcmp(cmd, "explain")) {
        if (argc < 3) {
            fputs("qport: explain needs a rule id, e.g. qport explain QNX020\n", stderr);
            return 2;
        }
        strbuf out;
        sb_init(&out);
        int rc = report_explain(argv[2], &out);
        if (rc != 0) {
            fprintf(stderr, "qport: no rule '%s' (see qport rules)\n", argv[2]);
            sb_free(&out);
            return 2;
        }
        fputs(out.data, stdout);
        sb_free(&out);
        return 0;
    }

    int argi = 2;
    int toolchain = 0;
    if (!strcmp(cmd, "toolchain"))
        toolchain = 1;
    else if (strcmp(cmd, "scan") != 0)
        argi = 1; /* `qport <path>` is short for `qport scan <path>` */

    out_format fmt = FMT_TEXT;
    const char *outfile = NULL, *path = NULL, *sysroot = getenv("QNX_TARGET");
    const char *arch = "aarch64le", *outdir = ".";
    int no_sysroot = 0, show_guarded = 0;
    severity fail_on = SEV_ERROR;
    int fail_never = 0;
    long threshold = 65536;
    const char *excl_default[] = { "build", "node_modules", "CMakeFiles" };
    const char **excludes = xmalloc(sizeof(char *) * (size_t)(argc + 8));
    size_t n_ex = 0;
    for (size_t k = 0; k < sizeof excl_default / sizeof excl_default[0]; k++)
        excludes[n_ex++] = excl_default[k];

    for (int i = argi; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-f") || !strcmp(a, "--format")) {
            if (!need_arg(i, argc, a)) return 2;
            const char *v = argv[++i];
            if (!strcmp(v, "text")) fmt = FMT_TEXT;
            else if (!strcmp(v, "brief")) fmt = FMT_BRIEF;
            else if (!strcmp(v, "json")) fmt = FMT_JSON;
            else if (!strcmp(v, "sarif")) fmt = FMT_SARIF;
            else if (!strcmp(v, "md") || !strcmp(v, "markdown")) fmt = FMT_MARKDOWN;
            else if (!strcmp(v, "html")) fmt = FMT_HTML;
            else {
                fprintf(stderr, "qport: unknown format '%s'\n", v);
                return 2;
            }
        } else if (!strcmp(a, "-o") || !strcmp(a, "--output")) {
            if (!need_arg(i, argc, a)) return 2;
            outfile = argv[++i];
        } else if (!strcmp(a, "--brief")) {
            fmt = FMT_BRIEF;
        } else if (!strcmp(a, "--sysroot")) {
            if (!need_arg(i, argc, a)) return 2;
            sysroot = argv[++i];
        } else if (!strcmp(a, "--no-sysroot")) {
            no_sysroot = 1;
        } else if (!strcmp(a, "--stack-threshold")) {
            if (!need_arg(i, argc, a)) return 2;
            char *end;
            threshold = strtol(argv[++i], &end, 10);
            if (*end || threshold <= 0) {
                fprintf(stderr, "qport: bad --stack-threshold '%s'\n", argv[i]);
                return 2;
            }
        } else if (!strcmp(a, "--show-guarded")) {
            show_guarded = 1;
        } else if (!strcmp(a, "--exclude")) {
            if (!need_arg(i, argc, a)) return 2;
            excludes[n_ex++] = argv[++i];
        } else if (!strcmp(a, "--fail-on")) {
            if (!need_arg(i, argc, a)) return 2;
            const char *v = argv[++i];
            if (!strcmp(v, "error")) fail_on = SEV_ERROR;
            else if (!strcmp(v, "warning")) fail_on = SEV_WARN;
            else if (!strcmp(v, "note")) fail_on = SEV_NOTE;
            else if (!strcmp(v, "never")) fail_never = 1;
            else {
                fprintf(stderr, "qport: bad --fail-on '%s'\n", v);
                return 2;
            }
        } else if (!strcmp(a, "--color")) {
            color = 1;
        } else if (!strcmp(a, "--no-color")) {
            color = 0;
        } else if (!strcmp(a, "--arch")) {
            if (!need_arg(i, argc, a)) return 2;
            arch = argv[++i];
        } else if (!strcmp(a, "--out")) {
            if (!need_arg(i, argc, a)) return 2;
            outdir = argv[++i];
        } else if (a[0] == '-' && a[1]) {
            fprintf(stderr, "qport: unknown option '%s'\n", a);
            return 2;
        } else if (!path) {
            path = a;
        } else {
            fprintf(stderr, "qport: only one path, got '%s' and '%s'\n", path, a);
            return 2;
        }
    }
    if (!path) {
        usage(stderr);
        return 2;
    }
    if (no_sysroot || (sysroot && !sysroot[0]))
        sysroot = NULL;
    if (sysroot) {
        char inc[4096];
        struct stat st;
        snprintf(inc, sizeof inc, "%s/usr/include", sysroot);
        if (stat(inc, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "qport: %s has no usr/include; is it a QNX 8.0 target directory?\n", sysroot);
            return 2;
        }
    }
    if (outfile || (fmt != FMT_TEXT && fmt != FMT_BRIEF))
        color = color && !outfile && (fmt == FMT_TEXT || fmt == FMT_BRIEF);

    scan_options opt = { sysroot, threshold, show_guarded, excludes, n_ex,
                         isatty(STDERR_FILENO) && !getenv("QPORT_NO_PROGRESS") };
    scan_result res;
    if (scan_path(path, &opt, &res) != 0) {
        scan_result_free(&res);
        free(excludes);
        return 2;
    }

    if (toolchain) {
        int rc = emit_toolchain(outdir, arch, &res.facts);
        scan_result_free(&res);
        free(excludes);
        return rc == 0 ? 0 : 2;
    }

    strbuf out;
    sb_init(&out);
    report_write(&res, fmt, color, show_guarded, &out);
    int rc = 0;
    if (outfile) {
        FILE *fp = fopen(outfile, "w");
        if (!fp || fputs(out.data, fp) < 0 || fclose(fp) != 0) {
            perror(outfile);
            rc = 2;
        }
    } else {
        fputs(out.data, stdout);
    }
    if (rc == 0 && !fail_never) {
        for (int s = SEV_ERROR; s >= (int)fail_on; s--)
            if (count_sev(&res, (severity)s, show_guarded))
                rc = 1;
    }
    sb_free(&out);
    scan_result_free(&res);
    free(excludes);
    return rc;
}
