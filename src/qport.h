/*
 * qport - find what stops Linux C/C++ code from building or behaving the
 * same on QNX OS 8.0, and say what to use instead.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef QPORT_H
#define QPORT_H

#include <stddef.h>

#define QPORT_VERSION "0.1.0"

/* ---------- small utilities (util.c) ---------- */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} strbuf;

void  sb_init(strbuf *sb);
void  sb_free(strbuf *sb);
void  sb_putc(strbuf *sb, char c);
void  sb_puts(strbuf *sb, const char *s);
void  sb_putn(strbuf *sb, const char *s, size_t n);
void  sb_printf(strbuf *sb, const char *fmt, ...);
void  sb_json_str(strbuf *sb, const char *s); /* appends a quoted JSON string */

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *read_file(const char *path, size_t *len_out);
int   str_ends_with(const char *s, const char *suffix);
int   str_starts_with(const char *s, const char *prefix);

/* ---------- lexer (lexer.c) ---------- */

typedef enum {
    TK_IDENT,
    TK_NUMBER,
    TK_STRING,   /* text holds the literal's contents without quotes */
    TK_CHAR,
    TK_PUNCT,    /* text holds one punctuation character */
    TK_PP        /* a whole preprocessor directive; text = directive after '#',
                    comments removed, continuation lines joined */
} tok_kind;

typedef struct {
    tok_kind kind;
    char    *text;
    int      line;
    int      col;
} token;

typedef struct {
    token *v;
    size_t n;
    size_t cap;
} token_list;

/* Tokenize C/C++ source. Comments are dropped. Never fails on bad input. */
void lex_source(const char *src, size_t len, token_list *out);
void tokens_free(token_list *tl);

/* ---------- rules (rules.c) ---------- */

typedef enum { SEV_NOTE = 0, SEV_WARN = 1, SEV_ERROR = 2 } severity;

typedef enum {
    RK_INCLUDE,        /* exact header name, e.g. sys/epoll.h */
    RK_INCLUDE_PREFIX, /* header prefix, e.g. linux/ */
    RK_CALL,           /* identifier used as a call: name( */
    RK_IDENT,          /* identifier used anywhere (macros, types) */
    RK_STRING_PREFIX,  /* string literal starting with this, e.g. /sys/ */
    RK_DEFINE,         /* #define NAME */
    RK_BUILD_TEXT,     /* substring in a build file, e.g. -fsplit-stack */
    RK_SPECIAL         /* produced by dedicated checks in scan.c */
} rule_kind;

typedef struct {
    const char *id;
    severity    sev;
    rule_kind   kind;
    const char *pattern;   /* NULL-terminated, '|'-separated alternatives */
    const char *title;     /* one line: what is wrong */
    const char *fix;       /* what to do on QNX 8.0 */
    const char *ref;       /* where in the QNX docs this comes from */
} rule;

extern const rule qport_rules[];
extern const size_t qport_rule_count;
const rule *rule_by_id(const char *id);
const char *sev_name(severity s);

/* ---------- scanning (scan.c) ---------- */

typedef struct {
    const rule *r;
    severity    sev;      /* may be lowered from r->sev, e.g. when guarded */
    char       *file;     /* path relative to the scan root */
    int         line;
    int         col;
    char       *what;     /* the matched text, e.g. "epoll_create1" */
    char       *snippet;  /* the source line, trimmed */
    int         guarded;  /* inside a branch that QNX builds do not compile */
} finding;

typedef struct {
    finding *v;
    size_t   n;
    size_t   cap;
} finding_list;

typedef struct {
    const char *sysroot;           /* QNX target dir for the header oracle, or NULL */
    long        stack_threshold;   /* bytes; local arrays at or above this are flagged */
    int         include_guarded;   /* report findings inside non-QNX #if branches */
    const char **excludes;         /* directory names to skip */
    size_t      n_excludes;
    int         progress;          /* draw a progress line on stderr */
} scan_options;

typedef struct {
    /* what the code needs from QNX libraries outside libc */
    int uses_sockets, uses_regex, uses_inotify;
    int links_socket, links_regex, links_fsnotify;
    int has_cmake, has_make, has_meson, has_autotools;
} build_facts;

typedef struct {
    char  *path;   /* relative to the scan root */
    size_t lines;
    int    build;  /* a build file rather than C/C++ source */
} file_info;

typedef struct {
    char        *root;
    file_info   *files;
    size_t       n_files, cap_files;
    size_t       files_scanned;
    size_t       source_files;
    size_t       build_files;
    size_t       lines_scanned;
    finding_list findings;
    build_facts  facts;
    char       **headers;   /* project header paths, for the sysroot oracle */
    size_t       n_headers;
    size_t       cap_headers;
} scan_result;

int  scan_path(const char *path, const scan_options *opt, scan_result *res);
void scan_result_free(scan_result *res);
void scan_sort(scan_result *res);
size_t count_sev(const scan_result *res, severity s, int include_guarded);

/* ---------- reports (report.c) ---------- */

typedef enum { FMT_TEXT, FMT_BRIEF, FMT_JSON, FMT_SARIF, FMT_MARKDOWN, FMT_HTML } out_format;

extern const char qport_html_template[]; /* generated from report/template.html */

void report_write(const scan_result *res, out_format fmt, int color,
                  int show_guarded, strbuf *out);
void report_rules(int color, strbuf *out);
int  report_explain(const char *id, strbuf *out);

/* ---------- QNX build config generator (emit.c) ---------- */

int emit_toolchain(const char *path, const char *arch, const build_facts *f);

#endif
