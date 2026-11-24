/* SPDX-License-Identifier: MIT
 *
 * Walks a source tree, tokenizes C/C++ files, tracks #if branches to know
 * which code a QNX build actually compiles, and applies the rule set.
 */
#include "qport.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

/* Does `word` equal one of the '|'-separated alternatives in `alts`? */
static int alt_match(const char *alts, const char *word, size_t wlen, int prefix)
{
    const char *p = alts;
    while (*p) {
        const char *e = strchr(p, '|');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (prefix) {
            if (wlen >= n && strncmp(word, p, n) == 0)
                return 1;
        } else if (n == wlen && strncmp(word, p, n) == 0) {
            return 1;
        }
        if (!e)
            break;
        p = e + 1;
    }
    return 0;
}

static int word_in(const char *word, const char *const *list)
{
    for (; *list; list++)
        if (strcmp(word, *list) == 0)
            return 1;
    return 0;
}

/* `// qport:ignore` silences every rule on that line;
 * `// qport:ignore QNX020 QNX021` only the listed ones. */
static int suppressed(const char *line, const char *id)
{
    const char *p = strstr(line, "qport:ignore");
    if (!p)
        return 0;
    p += strlen("qport:ignore");
    if (*p == '-') /* qport:ignore-file is handled per file */
        return 0;
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '=')
        p++;
    if (strncmp(p, "QNX", 3) != 0)
        return 1;
    size_t n = strlen(id);
    while (strncmp(p, "QNX", 3) == 0) {
        if (strncmp(p, id, n) == 0 && !isalnum((unsigned char)p[n]))
            return 1;
        while (*p && *p != ' ' && *p != ',')
            p++;
        while (*p == ' ' || *p == ',')
            p++;
    }
    return 0;
}

static void add_finding(scan_result *res, const rule *r, severity sev,
                        const char *file, int line, int col, const char *what,
                        const char *snippet, int guarded)
{
    if (snippet && suppressed(snippet, r->id))
        return;
    finding_list *fl = &res->findings;
    for (size_t i = 0; i < fl->n; i++) {
        finding *f = &fl->v[i];
        if (f->r == r && f->line == line && strcmp(f->file, file) == 0 &&
            strcmp(f->what, what) == 0)
            return; /* duplicate */
    }
    if (fl->n == fl->cap) {
        fl->cap = fl->cap ? fl->cap * 2 : 64;
        fl->v = xrealloc(fl->v, fl->cap * sizeof *fl->v);
    }
    finding *f = &fl->v[fl->n++];
    f->r       = r;
    f->sev     = guarded ? SEV_NOTE : sev;
    f->file    = xstrdup(file);
    f->line    = line;
    f->col     = col;
    if (strlen(what) > 60) {
        char *w = xstrndup(what, 60);
        f->what = xmalloc(64);
        snprintf(f->what, 64, "%s...", w);
        free(w);
    } else {
        f->what = xstrdup(what);
    }
    f->snippet = xstrdup(snippet ? snippet : "");
    f->guarded = guarded;
}

/* Line table so findings can show the offending source line. */
typedef struct {
    const char *src;
    size_t      len;
    size_t     *start;
    size_t      n;
} line_table;

static void lines_build(line_table *lt, const char *src, size_t len)
{
    lt->src = src;
    lt->len = len;
    size_t cap = 256;
    lt->start = xmalloc(cap * sizeof *lt->start);
    lt->n = 0;
    lt->start[lt->n++] = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '\n') {
            if (lt->n == cap) {
                cap *= 2;
                lt->start = xrealloc(lt->start, cap * sizeof *lt->start);
            }
            lt->start[lt->n++] = i + 1;
        }
    }
}

static char *line_raw(const line_table *lt, int line)
{
    if (line < 1 || (size_t)line > lt->n)
        return xstrdup("");
    size_t a = lt->start[line - 1];
    size_t b = a;
    while (b < lt->len && lt->src[b] != '\n')
        b++;
    return xstrndup(lt->src + a, b - a);
}

static char *line_text(const line_table *lt, int line)
{
    if (line < 1 || (size_t)line > lt->n)
        return xstrdup("");
    size_t a = lt->start[line - 1];
    size_t b = a;
    while (b < lt->len && lt->src[b] != '\n' && lt->src[b] != '\r')
        b++;
    while (a < b && isspace((unsigned char)lt->src[a]))
        a++;
    while (b > a && isspace((unsigned char)lt->src[b - 1]))
        b--;
    if (b - a > 160)
        b = a + 160;
    return xstrndup(lt->src + a, b - a);
}

/* ------------------------------------------------------------------ */
/* #if branch analysis                                                */
/* ------------------------------------------------------------------ */

typedef enum { TV_FALSE = 0, TV_TRUE = 1, TV_UNKNOWN = 2 } tristate;

static const char *const qnx_defined[] = {
    "__QNX__", "__QNXNTO__", "__unix__", "__unix", "__ELF__", NULL
};

/* Macros that a QNX 8.0 build does not define. */
static const char *const not_on_qnx[] = {
    "__linux__", "__linux", "linux", "__gnu_linux__", "__ANDROID__",
    "__APPLE__", "__MACH__", "_WIN32", "_WIN64", "__CYGWIN__", "__MINGW32__",
    "__FreeBSD__", "__NetBSD__", "__OpenBSD__", "__DragonFly__", "__sun",
    "__HAIKU__", "_AIX", "__hpux", "__GLIBC__", "__GNU_LIBRARY__",
    "__EMSCRIPTEN__", "__Fuchsia__", "__ZEPHYR__", "_MSC_VER", NULL
};

typedef struct {
    const char *s;
    size_t      i;
} cexpr;

static void ce_ws(cexpr *e)
{
    while (e->s[e->i] && isspace((unsigned char)e->s[e->i]))
        e->i++;
}

static int ce_ident(cexpr *e, char *buf, size_t cap)
{
    ce_ws(e);
    size_t n = 0;
    const char *s = e->s;
    if (!(isalpha((unsigned char)s[e->i]) || s[e->i] == '_'))
        return 0;
    while (isalnum((unsigned char)s[e->i]) || s[e->i] == '_') {
        if (n + 1 < cap)
            buf[n++] = s[e->i];
        e->i++;
    }
    buf[n] = '\0';
    return 1;
}

static tristate macro_value(const char *name)
{
    if (word_in(name, qnx_defined))
        return TV_TRUE;
    if (word_in(name, not_on_qnx))
        return TV_FALSE;
    return TV_UNKNOWN;
}

static tristate ce_or(cexpr *e);

static tristate ce_primary(cexpr *e)
{
    ce_ws(e);
    const char *s = e->s;
    char id[128];
    if (s[e->i] == '!') {
        e->i++;
        tristate v = ce_primary(e);
        return v == TV_UNKNOWN ? TV_UNKNOWN : (v == TV_TRUE ? TV_FALSE : TV_TRUE);
    }
    if (s[e->i] == '(') {
        e->i++;
        tristate v = ce_or(e);
        ce_ws(e);
        if (s[e->i] == ')')
            e->i++;
        return v;
    }
    if (isdigit((unsigned char)s[e->i])) {
        long v = strtol(s + e->i, NULL, 0);
        while (isalnum((unsigned char)s[e->i]))
            e->i++;
        ce_ws(e);
        if (strchr("<>=!", s[e->i]) && s[e->i])
            return TV_UNKNOWN;
        return v ? TV_TRUE : TV_FALSE;
    }
    size_t save = e->i;
    if (ce_ident(e, id, sizeof id)) {
        if (strcmp(id, "defined") == 0) {
            ce_ws(e);
            int paren = 0;
            if (s[e->i] == '(') {
                paren = 1;
                e->i++;
            }
            char name[128];
            if (!ce_ident(e, name, sizeof name))
                return TV_UNKNOWN;
            ce_ws(e);
            if (paren && s[e->i] == ')')
                e->i++;
            return macro_value(name);
        }
        /* bare macro, possibly compared: __GLIBC__ >= 2, __linux__ == 1 */
        tristate v = macro_value(id);
        ce_ws(e);
        if (s[e->i] && strchr("<>=!", s[e->i]) && s[e->i + 1] != '\0' &&
            !(s[e->i] == '!' && s[e->i + 1] != '=')) {
            /* consume the comparison and the right-hand side atom */
            char op[3] = { s[e->i], 0, 0 };
            e->i++;
            if (s[e->i] == '=') {
                op[1] = '=';
                e->i++;
            }
            ce_ws(e);
            long rhs = 0;
            int rhs_num = isdigit((unsigned char)s[e->i]);
            if (rhs_num)
                rhs = strtol(s + e->i, NULL, 0);
            while (isalnum((unsigned char)s[e->i]) || s[e->i] == '_')
                e->i++;
            if (v == TV_FALSE && rhs_num) {
                /* undefined macro evaluates to 0 */
                long l = 0;
                int r;
                if (!strcmp(op, ">="))      r = l >= rhs;
                else if (!strcmp(op, ">"))  r = l > rhs;
                else if (!strcmp(op, "<=")) r = l <= rhs;
                else if (!strcmp(op, "<"))  r = l < rhs;
                else if (!strcmp(op, "==")) r = l == rhs;
                else if (!strcmp(op, "!=")) r = l != rhs;
                else return TV_UNKNOWN;
                return r ? TV_TRUE : TV_FALSE;
            }
            return TV_UNKNOWN;
        }
        if (v == TV_UNKNOWN && s[e->i] == '(') {
            /* function-like macro or __has_include(...): skip the call */
            int depth = 0;
            do {
                if (s[e->i] == '(') depth++;
                else if (s[e->i] == ')') depth--;
                e->i++;
            } while (s[e->i] && depth > 0);
        }
        return v;
    }
    (void)save;
    /* unknown token: skip one char so we always make progress */
    if (s[e->i])
        e->i++;
    return TV_UNKNOWN;
}

static tristate ce_and(cexpr *e)
{
    tristate v = ce_primary(e);
    for (;;) {
        ce_ws(e);
        if (e->s[e->i] == '&' && e->s[e->i + 1] == '&') {
            e->i += 2;
            tristate r = ce_primary(e);
            if (v == TV_FALSE || r == TV_FALSE)
                v = TV_FALSE;
            else if (v == TV_TRUE && r == TV_TRUE)
                v = TV_TRUE;
            else
                v = TV_UNKNOWN;
        } else {
            return v;
        }
    }
}

static tristate ce_or(cexpr *e)
{
    tristate v = ce_and(e);
    for (;;) {
        ce_ws(e);
        if (e->s[e->i] == '|' && e->s[e->i + 1] == '|') {
            e->i += 2;
            tristate r = ce_and(e);
            if (v == TV_TRUE || r == TV_TRUE)
                v = TV_TRUE;
            else if (v == TV_FALSE && r == TV_FALSE)
                v = TV_FALSE;
            else
                v = TV_UNKNOWN;
        } else {
            return v;
        }
    }
}

/* Evaluate a #if expression as a QNX 8.0 build would see it. Anything
 * the evaluator does not understand makes the result UNKNOWN, which is
 * treated as "compiled" (we would rather report than hide). */
static tristate eval_on_qnx(const char *expr)
{
    cexpr e = { expr, 0 };
    tristate v = ce_or(&e);
    ce_ws(&e);
    if (e.s[e.i] != '\0') /* trailing parts we did not parse */
        return v == TV_FALSE ? TV_UNKNOWN : (v == TV_TRUE ? TV_UNKNOWN : v);
    return v;
}

typedef struct {
    tristate cur;        /* value of the branch we are in */
    int      any_true;   /* an earlier branch was definitely taken */
    int      all_false;  /* every earlier branch was definitely not taken */
} cond_frame;

typedef struct {
    cond_frame *v;
    size_t      n, cap;
} cond_stack;

static void cond_push(cond_stack *cs, tristate v)
{
    if (cs->n == cs->cap) {
        cs->cap = cs->cap ? cs->cap * 2 : 16;
        cs->v = xrealloc(cs->v, cs->cap * sizeof *cs->v);
    }
    cs->v[cs->n].cur = v;
    cs->v[cs->n].any_true = v == TV_TRUE;
    cs->v[cs->n].all_false = v == TV_FALSE;
    cs->n++;
}

static int cond_excluded(const cond_stack *cs)
{
    for (size_t i = 0; i < cs->n; i++)
        if (cs->v[i].cur == TV_FALSE)
            return 1;
    return 0;
}

static void cond_directive(cond_stack *cs, const char *name, const char *rest)
{
    if (!strcmp(name, "if")) {
        cond_push(cs, eval_on_qnx(rest));
    } else if (!strcmp(name, "ifdef") || !strcmp(name, "ifndef")) {
        char id[128];
        size_t n = 0;
        while (rest[n] && (isalnum((unsigned char)rest[n]) || rest[n] == '_') && n < sizeof id - 1) {
            id[n] = rest[n];
            n++;
        }
        id[n] = '\0';
        tristate v = macro_value(id);
        if (!strcmp(name, "ifndef") && v != TV_UNKNOWN)
            v = v == TV_TRUE ? TV_FALSE : TV_TRUE;
        cond_push(cs, v);
    } else if (!strcmp(name, "elif") || !strcmp(name, "elifdef") || !strcmp(name, "elifndef")) {
        if (!cs->n)
            return;
        cond_frame *f = &cs->v[cs->n - 1];
        tristate v;
        if (f->any_true) {
            v = TV_FALSE;
        } else {
            tristate x;
            if (!strcmp(name, "elif")) {
                x = eval_on_qnx(rest);
            } else {
                char id[128];
                size_t n = 0;
                while (rest[n] && (isalnum((unsigned char)rest[n]) || rest[n] == '_') && n < sizeof id - 1) {
                    id[n] = rest[n];
                    n++;
                }
                id[n] = '\0';
                x = macro_value(id);
                if (!strcmp(name, "elifndef") && x != TV_UNKNOWN)
                    x = x == TV_TRUE ? TV_FALSE : TV_TRUE;
            }
            if (f->all_false)
                v = x;
            else
                v = x == TV_FALSE ? TV_FALSE : TV_UNKNOWN;
        }
        f->cur = v;
        if (v == TV_TRUE)
            f->any_true = 1;
        if (v != TV_FALSE)
            f->all_false = 0;
    } else if (!strcmp(name, "else")) {
        if (!cs->n)
            return;
        cond_frame *f = &cs->v[cs->n - 1];
        f->cur = f->any_true ? TV_FALSE : (f->all_false ? TV_TRUE : TV_UNKNOWN);
    } else if (!strcmp(name, "endif")) {
        if (cs->n)
            cs->n--;
    }
}

/* ------------------------------------------------------------------ */
/* integer constant evaluation for array sizes                        */
/* ------------------------------------------------------------------ */

/* Project-wide knowledge used to size stack arrays: integer object-like
 * macros (#define N 1024) and simple struct layouts. Filled by a pre-pass
 * over every source file before the main scan. */
typedef struct {
    char      *name;
    token_list value;
} macro_def;

typedef struct {
    char     *name;   /* struct tag or typedef name */
    long long size;
    int       align;
} struct_def;

static macro_def  *g_macros;
static size_t      g_n_macros, g_cap_macros;
static struct_def *g_structs;
static size_t      g_n_structs, g_cap_structs;

static const macro_def *find_macro(const char *name)
{
    for (size_t i = g_n_macros; i-- > 0;)
        if (!strcmp(g_macros[i].name, name))
            return &g_macros[i];
    return NULL;
}

static const struct_def *find_struct(const char *name)
{
    for (size_t i = g_n_structs; i-- > 0;)
        if (!strcmp(g_structs[i].name, name))
            return &g_structs[i];
    return NULL;
}

static int type_size_align(const char *const *words, size_t n, int pointer, int *align);

/* Index of the ']' closing the '[' at `open`, searching before `limit`. */
static size_t match_close_tokens(const token_list *tl, size_t open, size_t limit)
{
    int depth = 0;
    for (size_t i = open; i < limit; i++) {
        if (tl->v[i].kind != TK_PUNCT)
            continue;
        if (tl->v[i].text[0] == '[') depth++;
        else if (tl->v[i].text[0] == ']' && --depth == 0) return i;
    }
    return limit;
}

typedef struct {
    const token *t;
    size_t       i, end;
    int          unknown; /* saw a macro or something we cannot evaluate */
    int          runtime; /* saw a lowercase identifier: likely a VLA */
    int          depth;   /* macro expansion depth */
} ievalr;

static long long ie_add(ievalr *e);

static int parse_int_literal(const char *s, long long *out)
{
    char buf[64];
    size_t n = 0;
    for (const char *p = s; *p && n < sizeof buf - 1; p++)
        if (*p != '\'')
            buf[n++] = *p;
    buf[n] = '\0';
    char *end;
    errno = 0;
    long long v;
    if (n > 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B'))
        v = strtoll(buf + 2, &end, 2);
    else
        v = strtoll(buf, &end, 0);
    if (errno)
        return 0;
    for (; *end; end++)
        if (!strchr("uUlLzZ", *end))
            return 0; /* float or odd literal */
    *out = v;
    return 1;
}

static long long ie_primary(ievalr *e)
{
    if (e->i >= e->end) {
        e->unknown = 1;
        return 0;
    }
    const token *t = &e->t[e->i];
    if (t->kind == TK_NUMBER) {
        long long v;
        e->i++;
        if (!parse_int_literal(t->text, &v)) {
            e->unknown = 1;
            return 0;
        }
        return v;
    }
    if (t->kind == TK_PUNCT && t->text[0] == '(') {
        e->i++;
        long long v = ie_add(e);
        if (e->i < e->end && e->t[e->i].kind == TK_PUNCT && e->t[e->i].text[0] == ')')
            e->i++;
        return v;
    }
    if (t->kind == TK_IDENT && !strcmp(t->text, "sizeof")) {
        /* sizeof(type) for basic and known struct types */
        e->i++;
        if (e->i >= e->end || e->t[e->i].kind != TK_PUNCT || e->t[e->i].text[0] != '(') {
            e->unknown = 1;
            return 0;
        }
        size_t close = e->i;
        int d = 0;
        for (; close < e->end; close++) {
            if (e->t[close].kind == TK_PUNCT && e->t[close].text[0] == '(') d++;
            else if (e->t[close].kind == TK_PUNCT && e->t[close].text[0] == ')' && --d == 0) break;
        }
        const char *words[8];
        size_t nw = 0;
        int ptr = 0, ok = 1;
        for (size_t k = e->i + 1; k < close; k++) {
            if (e->t[k].kind == TK_IDENT && nw < 8)
                words[nw++] = e->t[k].text;
            else if (e->t[k].kind == TK_PUNCT && e->t[k].text[0] == '*')
                ptr = 1;
            else
                ok = 0;
        }
        e->i = close < e->end ? close + 1 : e->end;
        int al;
        int sz = ok && nw ? type_size_align(words, nw, ptr, &al) : 0;
        if (sz <= 0) {
            e->unknown = 1;
            return 0;
        }
        return sz;
    }
    if (t->kind == TK_IDENT) {
        e->i++;
        const macro_def *m = e->depth < 8 ? find_macro(t->text) : NULL;
        if (m) {
            ievalr sub = { m->value.v, 0, m->value.n, 0, 0, e->depth + 1 };
            long long v = ie_add(&sub);
            if (sub.i != sub.end || sub.unknown || sub.runtime)
                e->unknown = 1;
            return v;
        }
        int has_lower = 0;
        for (const char *p = t->text; *p; p++)
            if (islower((unsigned char)*p))
                has_lower = 1;
        if (has_lower)
            e->runtime = 1;
        else
            e->unknown = 1;
        return 0;
    }
    e->unknown = 1;
    e->i++;
    return 0;
}

static long long ie_mul(ievalr *e)
{
    long long v = ie_primary(e);
    while (e->i < e->end && e->t[e->i].kind == TK_PUNCT &&
           strchr("*/", e->t[e->i].text[0])) {
        char op = e->t[e->i].text[0];
        e->i++;
        long long r = ie_primary(e);
        if (op == '*')
            v = (v > 0 && r > 0 && v > (1LL << 40) / r) ? (1LL << 40) : v * r;
        else
            v = r ? v / r : 0;
    }
    return v;
}

static long long ie_shift(ievalr *e)
{
    long long v = ie_mul(e);
    /* a << b arrives as two '<' tokens */
    while (e->i + 1 < e->end && e->t[e->i].kind == TK_PUNCT &&
           e->t[e->i + 1].kind == TK_PUNCT &&
           ((e->t[e->i].text[0] == '<' && e->t[e->i + 1].text[0] == '<') ||
            (e->t[e->i].text[0] == '>' && e->t[e->i + 1].text[0] == '>'))) {
        char op = e->t[e->i].text[0];
        e->i += 2;
        long long r = ie_mul(e);
        if (r < 0 || r > 40) {
            e->unknown = 1;
            return 0;
        }
        v = op == '<' ? v << r : v >> r;
    }
    return v;
}

static long long ie_add(ievalr *e)
{
    long long v = ie_shift(e);
    while (e->i < e->end && e->t[e->i].kind == TK_PUNCT &&
           strchr("+-", e->t[e->i].text[0])) {
        char op = e->t[e->i].text[0];
        e->i++;
        long long r = ie_shift(e);
        v = op == '+' ? v + r : v - r;
    }
    return v;
}

/* Size of a declared element type. Returns 0 if unknown. */
static int type_size_align(const char *const *words, size_t n, int pointer, int *align)
{
    *align = 8;
    if (pointer)
        return 8;
    int longs = 0, sz = 0;
    const char *tag = NULL;
    for (size_t i = 0; i < n; i++) {
        const char *w = words[i];
        if (!strcmp(w, "char") || !strcmp(w, "int8_t") || !strcmp(w, "uint8_t") ||
            !strcmp(w, "bool") || !strcmp(w, "_Bool") || !strcmp(w, "u8") ||
            !strcmp(w, "byte") || !strcmp(w, "char8_t"))
            sz = 1;
        else if (!strcmp(w, "short") || !strcmp(w, "int16_t") || !strcmp(w, "uint16_t") ||
                 !strcmp(w, "char16_t"))
            sz = 2;
        else if (!strcmp(w, "int") || !strcmp(w, "unsigned") || !strcmp(w, "signed") ||
                 !strcmp(w, "float") || !strcmp(w, "int32_t") || !strcmp(w, "uint32_t") ||
                 !strcmp(w, "wchar_t") || !strcmp(w, "char32_t")) {
            if (!sz)
                sz = 4;
        } else if (!strcmp(w, "long"))
            longs++;
        else if (!strcmp(w, "double") || !strcmp(w, "int64_t") || !strcmp(w, "uint64_t") ||
                 !strcmp(w, "size_t") || !strcmp(w, "ssize_t") || !strcmp(w, "intptr_t") ||
                 !strcmp(w, "uintptr_t") || !strcmp(w, "off_t") || !strcmp(w, "ptrdiff_t"))
            sz = 8;
        else if (strcmp(w, "const") && strcmp(w, "volatile") && strcmp(w, "struct") &&
                 strcmp(w, "union") && strcmp(w, "register") && strcmp(w, "enum"))
            tag = w;
        else if (!strcmp(w, "enum"))
            sz = 4;
    }
    int r;
    if (longs && sz == 8) /* long double */
        r = 16;
    else if (longs)
        r = 8;
    else if (sz)
        r = sz;
    else if (tag && find_struct(tag)) {
        const struct_def *sd = find_struct(tag);
        *align = sd->align;
        return (int)(sd->size > 0x7fffffff ? 0x7fffffff : sd->size);
    } else
        return 0;
    *align = r > 8 ? 8 : r;
    return r;
}

static int type_size(const char *const *words, size_t n, int pointer)
{
    int al;
    int sz = type_size_align(words, n, pointer, &al);
    return sz ? sz : 1; /* unknown type: count at least one byte */
}

/* Pre-pass over one file's tokens: remember integer macros and struct layouts. */
static void learn_definitions(const token_list *tl)
{
    for (size_t i = 0; i < tl->n; i++) {
        const token *t = &tl->v[i];
        if (t->kind == TK_PP) {
            const char *d = t->text;
            if (strncmp(d, "define", 6) != 0 || !isspace((unsigned char)d[6]))
                continue;
            d += 6;
            while (isspace((unsigned char)*d))
                d++;
            size_t n = 0;
            while (isalnum((unsigned char)d[n]) || d[n] == '_')
                n++;
            if (!n || d[n] == '(') /* function-like macro */
                continue;
            if (g_n_macros == g_cap_macros) {
                g_cap_macros = g_cap_macros ? g_cap_macros * 2 : 64;
                g_macros = xrealloc(g_macros, g_cap_macros * sizeof *g_macros);
            }
            macro_def *m = &g_macros[g_n_macros++];
            m->name = xstrndup(d, n);
            memset(&m->value, 0, sizeof m->value);
            lex_source(d + n, strlen(d + n), &m->value);
            continue;
        }
        /* struct NAME { ... }   or   typedef struct [NAME] { ... } ALIAS; */
        if (t->kind != TK_IDENT || strcmp(t->text, "struct") != 0)
            continue;
        size_t j = i + 1;
        const char *tag = NULL;
        if (j < tl->n && tl->v[j].kind == TK_IDENT)
            tag = tl->v[j++].text;
        if (j >= tl->n || tl->v[j].kind != TK_PUNCT || tl->v[j].text[0] != '{')
            continue;
        size_t close = j;
        int depth = 0;
        for (; close < tl->n; close++) {
            if (tl->v[close].kind != TK_PUNCT)
                continue;
            if (tl->v[close].text[0] == '{') depth++;
            else if (tl->v[close].text[0] == '}' && --depth == 0) break;
        }
        if (close >= tl->n)
            continue;
        long long off = 0;
        int max_align = 1, ok = 1;
        size_t k = j + 1;
        while (ok && k < close) {
            const char *words[8];
            size_t nw = 0;
            int ptr = 0;
            size_t start = k;
            while (k < close && !(tl->v[k].kind == TK_PUNCT && tl->v[k].text[0] == ';'))
                k++;
            /* member: words [*] name [dims] ; */
            size_t m = start;
            while (m < k && tl->v[m].kind == TK_IDENT && nw < 8)
                words[nw++] = tl->v[m++].text;
            while (m < k && tl->v[m].kind == TK_PUNCT && tl->v[m].text[0] == '*') {
                ptr = 1;
                m++;
            }
            if (ptr) {
                if (m < k && tl->v[m].kind == TK_IDENT)
                    m++;
            } else if (nw >= 2) {
                nw--; /* last identifier is the member name */
            } else {
                ok = 0;
                break;
            }
            int al;
            long long sz = type_size_align(words, nw, ptr, &al);
            if (sz <= 0) {
                ok = 0;
                break;
            }
            while (m < k && tl->v[m].kind == TK_PUNCT && tl->v[m].text[0] == '[') {
                size_t c2 = match_close_tokens(tl, m, k);
                if (c2 >= k) { ok = 0; break; }
                ievalr e = { tl->v, m + 1, c2, 0, 0, 0 };
                long long v = ie_add(&e);
                if (e.unknown || e.runtime || e.i != c2 || v <= 0) { ok = 0; break; }
                sz *= v;
                m = c2 + 1;
            }
            if (!ok || m != k) { /* bit-fields, initialisers, nested types */
                ok = 0;
                break;
            }
            off = (off + al - 1) / al * al + sz;
            if (al > max_align)
                max_align = al;
            k++; /* past ';' */
        }
        if (!ok)
            continue;
        off = (off + max_align - 1) / max_align * max_align;
        const char *alias = NULL;
        if (close + 2 < tl->n && tl->v[close + 1].kind == TK_IDENT &&
            tl->v[close + 2].kind == TK_PUNCT && tl->v[close + 2].text[0] == ';' &&
            i > 0 && tl->v[i - 1].kind == TK_IDENT && !strcmp(tl->v[i - 1].text, "typedef"))
            alias = tl->v[close + 1].text;
        for (int pass = 0; pass < 2; pass++) {
            const char *nm = pass ? alias : tag;
            if (!nm)
                continue;
            if (g_n_structs == g_cap_structs) {
                g_cap_structs = g_cap_structs ? g_cap_structs * 2 : 32;
                g_structs = xrealloc(g_structs, g_cap_structs * sizeof *g_structs);
            }
            g_structs[g_n_structs].name = xstrdup(nm);
            g_structs[g_n_structs].size = off;
            g_structs[g_n_structs].align = max_align;
            g_n_structs++;
        }
    }
}

static void forget_definitions(void)
{
    for (size_t i = 0; i < g_n_macros; i++) {
        free(g_macros[i].name);
        tokens_free(&g_macros[i].value);
    }
    free(g_macros);
    g_macros = NULL;
    g_n_macros = g_cap_macros = 0;
    for (size_t i = 0; i < g_n_structs; i++)
        free(g_structs[i].name);
    free(g_structs);
    g_structs = NULL;
    g_n_structs = g_cap_structs = 0;
}

/* ------------------------------------------------------------------ */
/* source file scan                                                   */
/* ------------------------------------------------------------------ */

static const char *const socket_calls[] = {
    "socket", "socketpair", "getaddrinfo", "gethostbyname", "gethostbyname2",
    "gethostbyaddr", "getnameinfo", "inet_pton", "inet_ntop", "inet_addr",
    "inet_aton", "setsockopt", "getsockopt", "recvfrom", "sendto", "recvmsg",
    "sendmsg", "getifaddrs", "accept4", "if_nametoindex", NULL
};
static const char *const socket_headers[] = {
    "sys/socket.h", "netdb.h", "arpa/inet.h", "netinet/in.h", "netinet/tcp.h",
    "ifaddrs.h", "net/if.h", NULL
};
static const char *const exec_calls[] = {
    "execl", "execlp", "execle", "execv", "execvp", "execve", "execvpe", NULL
};
static const char *const not_types[] = {
    "return", "else", "do", "case", "goto", "sizeof", "typedef", "throw",
    "new", "delete", "co_return", "co_yield", "co_await", NULL
};

typedef struct {
    const char *file;
    int         line, col;
    char        snippet[200];
} location;

typedef struct {
    scan_result        *res;
    const scan_options *opt;
    const char         *rel;   /* path relative to root */
    line_table          lines;
    token_list          tl;
    cond_stack          cs;
    /* deferred per-file checks */
    int                 saw_exec;
    struct { size_t idx; int guarded; } *forks;
    size_t              n_fork, cap_fork;
} file_ctx;

static location first_socket_use, first_regex_use, first_inotify_use;
static char loc_files[3][1024];

static void note_use(location *loc, char *store, file_ctx *fc, int line, int col)
{
    if (loc->file)
        return;
    snprintf(store, 1024, "%s", fc->rel);
    loc->file = store;
    loc->line = line;
    loc->col = col;
    char *t = line_text(&fc->lines, line);
    snprintf(loc->snippet, sizeof loc->snippet, "%s", t);
    free(t);
}

static void emit(file_ctx *fc, const rule *r, int line, int col, const char *what)
{
    /* findings in branches a QNX build skips are kept, marked guarded, so
     * the report can show or count them */
    int guarded = cond_excluded(&fc->cs);
    char *raw = line_raw(&fc->lines, line);
    int quiet = suppressed(raw, r->id);
    free(raw);
    if (quiet)
        return;
    char *snip = line_text(&fc->lines, line);
    add_finding(fc->res, r, r->sev, fc->rel, line, col, what, snip, guarded);
    free(snip);
}

static int header_in_dir(const char *dir, const char *hdr)
{
    char path[4096];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, hdr);
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int header_in_project(const scan_result *res, const char *hdr)
{
    size_t hl = strlen(hdr);
    for (size_t i = 0; i < res->n_headers; i++) {
        const char *p = res->headers[i];
        size_t pl = strlen(p);
        if (pl == hl && strcmp(p, hdr) == 0)
            return 1;
        if (pl > hl && p[pl - hl - 1] == '/' && strcmp(p + pl - hl, hdr) == 0)
            return 1;
    }
    return 0;
}

static int header_in_sysroot(const char *sysroot, const char *hdr)
{
    char dir[4096];
    snprintf(dir, sizeof dir, "%s/usr/include", sysroot);
    if (header_in_dir(dir, hdr))
        return 1;
    /* C++ standard library headers live under usr/include/c++/<ver>/ */
    char cxx[4096];
    snprintf(cxx, sizeof cxx, "%s/usr/include/c++", sysroot);
    DIR *d = opendir(cxx);
    if (!d)
        return 0;
    struct dirent *de;
    int found = 0;
    while (!found && (de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        snprintf(dir, sizeof dir, "%s/%s", cxx, de->d_name);
        found = header_in_dir(dir, hdr);
    }
    closedir(d);
    return found;
}

static int sysroot_has_cxx(const char *sysroot)
{
    char cxx[4096];
    struct stat st;
    snprintf(cxx, sizeof cxx, "%s/usr/include/c++", sysroot);
    return stat(cxx, &st) == 0 && S_ISDIR(st.st_mode);
}

static void handle_include(file_ctx *fc, const token *t, const char *rest)
{
    while (*rest == ' ' || *rest == '\t')
        rest++;
    char close;
    if (*rest == '<')
        close = '>';
    else if (*rest == '"')
        close = '"';
    else
        return; /* computed include */
    const char *a = rest + 1;
    const char *b = strchr(a, close);
    if (!b)
        return;
    char *hdr = xstrndup(a, (size_t)(b - a));
    int matched = 0;

    for (size_t i = 0; i < qport_rule_count; i++) {
        const rule *r = &qport_rules[i];
        if ((r->kind == RK_INCLUDE && alt_match(r->pattern, hdr, strlen(hdr), 0)) ||
            (r->kind == RK_INCLUDE_PREFIX && alt_match(r->pattern, hdr, strlen(hdr), 1))) {
            if (close == '"' && header_in_project(fc->res, hdr))
                continue; /* a project file that happens to share the name */
            emit(fc, r, t->line, t->col, hdr);
            matched = 1;
        }
    }

    int excluded = cond_excluded(&fc->cs);
    if (!excluded) {
        if (word_in(hdr, socket_headers)) {
            fc->res->facts.uses_sockets = 1;
            note_use(&first_socket_use, loc_files[0], fc, t->line, t->col);
        }
        if (!strcmp(hdr, "regex.h")) {
            fc->res->facts.uses_regex = 1;
            note_use(&first_regex_use, loc_files[1], fc, t->line, t->col);
        }
        if (!strcmp(hdr, "sys/inotify.h")) {
            fc->res->facts.uses_inotify = 1;
            note_use(&first_inotify_use, loc_files[2], fc, t->line, t->col);
        }
    }

    /* header oracle against a real QNX target sysroot */
    const char *sr = fc->opt->sysroot;
    if (sr && !matched && !excluded && close == '>') {
        int has_ext = strchr(hdr, '.') != NULL;
        if ((has_ext || sysroot_has_cxx(sr)) &&
            !header_in_sysroot(sr, hdr) && !header_in_project(fc->res, hdr))
            emit(fc, rule_by_id("QNX090"), t->line, t->col, hdr);
    }
    free(hdr);
}

static void handle_directive(file_ctx *fc, const token *t)
{
    const char *d = t->text;
    char name[32];
    size_t n = 0;
    while (*d == ' ' || *d == '\t')
        d++;
    while (*d && (isalpha((unsigned char)*d) || *d == '_') && n < sizeof name - 1)
        name[n++] = *d++;
    name[n] = '\0';
    while (*d == ' ' || *d == '\t')
        d++;

    if (!strcmp(name, "include") || !strcmp(name, "include_next") || !strcmp(name, "import")) {
        handle_include(fc, t, d);
        return;
    }
    if (!strcmp(name, "define")) {
        char id[128];
        size_t k = 0;
        while (d[k] && (isalnum((unsigned char)d[k]) || d[k] == '_') && k < sizeof id - 1) {
            id[k] = d[k];
            k++;
        }
        id[k] = '\0';
        for (size_t i = 0; i < qport_rule_count; i++) {
            const rule *r = &qport_rules[i];
            if (r->kind == RK_DEFINE && alt_match(r->pattern, id, k, 0))
                emit(fc, r, t->line, t->col, id);
        }
        return;
    }
    cond_directive(&fc->cs, name, d);
}

static int is_punct(const token *t, char c)
{
    return t->kind == TK_PUNCT && t->text[0] == c;
}

/* Index of the token that closes the bracket opened at `open`. */
static size_t match_close(const token_list *tl, size_t open, char o, char c)
{
    int depth = 0;
    for (size_t i = open; i < tl->n; i++) {
        if (tl->v[i].kind == TK_PP)
            continue;
        if (is_punct(&tl->v[i], o))
            depth++;
        else if (is_punct(&tl->v[i], c) && --depth == 0)
            return i;
    }
    return tl->n;
}

/* Try to read a local array declaration starting at token i (statement
 * start, inside a function body). */
static void check_local_array(file_ctx *fc, size_t i)
{
    const token_list *tl = &fc->tl;
    const char *words[8];
    size_t nw = 0;
    size_t j = i;
    int pointer = 0;

    /* identifiers up to the first non-identifier: type words + maybe name */
    while (j < tl->n && tl->v[j].kind == TK_IDENT) {
        const char *w = tl->v[j].text;
        if (!strcmp(w, "static") || !strcmp(w, "thread_local") ||
            !strcmp(w, "_Thread_local") || !strcmp(w, "extern"))
            return; /* not on the stack */
        if (nw == 0 && word_in(w, not_types))
            return;
        if (nw < 8)
            words[nw++] = w;
        j++;
    }
    if (j + 1 < tl->n && is_punct(&tl->v[j], ':') && is_punct(&tl->v[j + 1], ':'))
        return; /* qualified name: std::array etc. are not C arrays */
    const token *name;
    if (j < tl->n && is_punct(&tl->v[j], '*')) {
        if (nw < 1)
            return;
        while (j < tl->n && is_punct(&tl->v[j], '*')) {
            pointer = 1;
            j++;
        }
        if (j >= tl->n || tl->v[j].kind != TK_IDENT)
            return;
        name = &tl->v[j];
        j++;
    } else {
        if (nw < 2)
            return; /* `x[i] = ...` is an expression, not a declaration */
        name = &tl->v[j - 1];
        nw--;
    }
    for (size_t k = 0; k < nw; k++)
        if (word_in(words[k], not_types))
            return;
    if (j >= tl->n || !is_punct(&tl->v[j], '['))
        return;

    long long total = type_size(words, nw, pointer);
    int unknown = 0, runtime = 0;
    while (j < tl->n && is_punct(&tl->v[j], '[')) {
        size_t close = match_close(tl, j, '[', ']');
        if (close >= tl->n)
            return;
        if (close == j + 1) /* int a[] = {...} */
            return;
        ievalr e = { tl->v, j + 1, close, 0, 0, 0 };
        long long v = ie_add(&e);
        if (e.i != close)
            e.unknown = 1;
        if (e.runtime)
            runtime = 1;
        else if (e.unknown)
            unknown = 1;
        else
            total = (v > 0 && total > (1LL << 40) / v) ? (1LL << 40) : total * v;
        j = close + 1;
    }
    if (runtime) {
        emit(fc, rule_by_id("QNX084"), name->line, name->col, name->text);
        return;
    }
    if (unknown)
        return;
    if (total >= fc->opt->stack_threshold) {
        char what[160];
        if (total >= 1024 * 1024)
            snprintf(what, sizeof what, "%s (%.1f MiB)", name->text, total / 1048576.0);
        else
            snprintf(what, sizeof what, "%s (%lld KiB)", name->text, total / 1024);
        emit(fc, rule_by_id("QNX083"), name->line, name->col, what);
    }
}

typedef struct {
    char *lhs;      /* variable the fd was assigned to, or NULL */
    int   has_dir;  /* O_DIRECTORY in the arguments */
    int   nested_in_dirfn;
    int   line, col;
    int   guarded;
} open_call;

static void scan_source(file_ctx *fc)
{
    token_list *tl = &fc->tl;
    /* brace stack: 1 = function body, 0 = other */
    unsigned char *braces = NULL;
    size_t nb = 0, capb = 0;
    int in_func = 0;

    open_call *opens = NULL;
    size_t n_open = 0, cap_open = 0;
    char **dir_fds = NULL;
    size_t n_dirfd = 0, cap_dirfd = 0;
    int nested_dirfn_depth_end = -1; /* token index where fdopendir( ... ) ends */

    for (size_t i = 0; i < tl->n; i++) {
        token *t = &tl->v[i];
        if (t->kind == TK_PP) {
            handle_directive(fc, t);
            continue;
        }
        const token *prev = NULL;
        for (size_t k = i; k-- > 0;) {
            if (tl->v[k].kind != TK_PP) {
                prev = &tl->v[k];
                break;
            }
        }
        const token *next = NULL;
        for (size_t k = i + 1; k < tl->n; k++) {
            if (tl->v[k].kind != TK_PP) {
                next = &tl->v[k];
                break;
            }
        }

        if (t->kind == TK_PUNCT) {
            if (t->text[0] == '{') {
                int fn = 0;
                if (prev && is_punct(prev, ')') && !in_func)
                    fn = 1;
                /* void f() const {, void f() noexcept {, void f() override { */
                if (!fn && !in_func && prev && prev->kind == TK_IDENT &&
                    (!strcmp(prev->text, "const") || !strcmp(prev->text, "noexcept") ||
                     !strcmp(prev->text, "override") || !strcmp(prev->text, "final")))
                    fn = 1;
                if (nb == capb) {
                    capb = capb ? capb * 2 : 32;
                    braces = xrealloc(braces, capb);
                }
                braces[nb++] = (unsigned char)fn;
                if (fn)
                    in_func = 1;
            } else if (t->text[0] == '}') {
                if (nb) {
                    nb--;
                    if (braces[nb])
                        in_func = 0;
                }
            }
            continue;
        }

        if (t->kind == TK_STRING) {
            for (size_t r = 0; r < qport_rule_count; r++) {
                const rule *ru = &qport_rules[r];
                if (ru->kind == RK_STRING_PREFIX &&
                    alt_match(ru->pattern, t->text, strlen(t->text), 1))
                    emit(fc, ru, t->line, t->col, t->text);
            }
            continue;
        }

        if (t->kind != TK_IDENT)
            continue;

        /* local array declarations at statement start inside functions */
        if (in_func && prev && (is_punct(prev, ';') || is_punct(prev, '{') || is_punct(prev, '}')))
            check_local_array(fc, i);

        int is_member = prev && (is_punct(prev, '.') ||
                                 (is_punct(prev, '>') && i >= 2 && is_punct(&tl->v[i - 2], '-')));
        int is_call = next && is_punct(next, '(') && !is_member;
        /* `int eventfd(...)` is a declaration or a shim definition */
        int is_decl = prev && prev->kind == TK_IDENT && !word_in(prev->text, not_types);
        if (prev && is_punct(prev, '*') && i >= 2 && tl->v[i - 2].kind == TK_IDENT &&
            !word_in(tl->v[i - 2].text, not_types))
            is_decl = 1;
        /* `::name(` is the global function, `obj::name(` is not ours */
        int scoped = prev && is_punct(prev, ':') && i >= 2 && is_punct(&tl->v[i - 2], ':') &&
                     !(i >= 3 && tl->v[i - 3].kind == TK_IDENT);

        if (is_call && !is_decl && (!prev || !is_punct(prev, ':') || scoped)) {
            for (size_t r = 0; r < qport_rule_count; r++) {
                const rule *ru = &qport_rules[r];
                if (ru->kind == RK_CALL && alt_match(ru->pattern, t->text, strlen(t->text), 0)) {
                    if (!strcmp(ru->id, "QNX034")) { /* fork: only report with exec */
                        if (fc->n_fork == fc->cap_fork) {
                            fc->cap_fork = fc->cap_fork ? fc->cap_fork * 2 : 8;
                            fc->forks = xrealloc(fc->forks, fc->cap_fork * sizeof *fc->forks);
                        }
                        fc->forks[fc->n_fork].idx = i;
                        fc->forks[fc->n_fork].guarded = cond_excluded(&fc->cs);
                        fc->n_fork++;
                        continue;
                    }
                    emit(fc, ru, t->line, t->col, t->text);
                }
            }
            int excluded = cond_excluded(&fc->cs);
            if (!excluded) {
                if (word_in(t->text, socket_calls)) {
                    fc->res->facts.uses_sockets = 1;
                    note_use(&first_socket_use, loc_files[0], fc, t->line, t->col);
                }
                if (!strcmp(t->text, "regcomp") || !strcmp(t->text, "regexec")) {
                    fc->res->facts.uses_regex = 1;
                    note_use(&first_regex_use, loc_files[1], fc, t->line, t->col);
                }
                if (!strncmp(t->text, "inotify_", 8)) {
                    fc->res->facts.uses_inotify = 1;
                    note_use(&first_inotify_use, loc_files[2], fc, t->line, t->col);
                }
            }
            if (word_in(t->text, exec_calls))
                fc->saw_exec = 1;

            /* open() of a directory without O_DIRECTORY */
            if (!strcmp(t->text, "fdopendir") || !strcmp(t->text, "fchdir")) {
                size_t close = match_close(tl, i + 1, '(', ')');
                if (i + 3 <= close && close < tl->n && tl->v[i + 2].kind == TK_IDENT &&
                    is_punct(&tl->v[i + 3], ')')) {
                    if (n_dirfd == cap_dirfd) {
                        cap_dirfd = cap_dirfd ? cap_dirfd * 2 : 8;
                        dir_fds = xrealloc(dir_fds, cap_dirfd * sizeof *dir_fds);
                    }
                    dir_fds[n_dirfd++] = tl->v[i + 2].text;
                }
                nested_dirfn_depth_end = (int)close;
            }
            if (!strcmp(t->text, "open") || !strcmp(t->text, "openat")) {
                size_t close = match_close(tl, i + 1, '(', ')');
                int has_dir = 0;
                for (size_t k = i + 2; k < close && k < tl->n; k++)
                    if (tl->v[k].kind == TK_IDENT && !strcmp(tl->v[k].text, "O_DIRECTORY"))
                        has_dir = 1;
                if (n_open == cap_open) {
                    cap_open = cap_open ? cap_open * 2 : 8;
                    opens = xrealloc(opens, cap_open * sizeof *opens);
                }
                open_call *oc = &opens[n_open++];
                oc->lhs = NULL;
                if (prev && is_punct(prev, '=') && i >= 2 && tl->v[i - 2].kind == TK_IDENT)
                    oc->lhs = tl->v[i - 2].text;
                oc->has_dir = has_dir;
                oc->nested_in_dirfn = nested_dirfn_depth_end >= 0 && (int)i < nested_dirfn_depth_end;
                oc->line = t->line;
                oc->col = t->col;
                oc->guarded = cond_excluded(&fc->cs);
            }
        }

        for (size_t r = 0; r < qport_rule_count; r++) {
            const rule *ru = &qport_rules[r];
            if (ru->kind == RK_IDENT && alt_match(ru->pattern, t->text, strlen(t->text), 0))
                emit(fc, ru, t->line, t->col, t->text);
        }
    }

    /* deferred: fork() only matters when followed by exec in this file */
    if (fc->saw_exec) {
        for (size_t k = 0; k < fc->n_fork; k++) {
            int guarded = fc->forks[k].guarded;
            token *t = &tl->v[fc->forks[k].idx];
            char *snip = line_text(&fc->lines, t->line);
            const rule *r = rule_by_id("QNX034");
            add_finding(fc->res, r, r->sev, fc->rel, t->line, t->col, t->text, snip, guarded);
            free(snip);
        }
    }
    /* deferred: directory fds */
    for (size_t k = 0; k < n_open; k++) {
        open_call *oc = &opens[k];
        if (oc->has_dir)
            continue;
        int is_dir = oc->nested_in_dirfn;
        for (size_t d = 0; !is_dir && d < n_dirfd; d++)
            if (oc->lhs && !strcmp(oc->lhs, dir_fds[d]))
                is_dir = 1;
        if (is_dir) {
            char *snip = line_text(&fc->lines, oc->line);
            const rule *r = rule_by_id("QNX085");
            add_finding(fc->res, r, r->sev, fc->rel, oc->line, oc->col, "open", snip, oc->guarded);
            free(snip);
        }
    }
    free(opens);
    free(dir_fds);
    free(braces);
}

/* ------------------------------------------------------------------ */
/* build files                                                        */
/* ------------------------------------------------------------------ */

static int is_word_char(int c)
{
    return isalnum(c) || c == '_' || c == '-';
}

static int mentions_lib(const char *line, const char *lib)
{
    /* -lsocket, or `socket` as a word on a link line */
    char flag[64];
    snprintf(flag, sizeof flag, "-l%s", lib);
    const char *p = line;
    size_t fl = strlen(flag);
    while ((p = strstr(p, flag))) {
        if (!is_word_char((unsigned char)p[fl]))
            return 1;
        p += fl;
    }
    if (strstr(line, "link") || strstr(line, "LIBS") || strstr(line, "find_library") ||
        strstr(line, "dependency") || strstr(line, "LDLIBS") || strstr(line, "LDFLAGS")) {
        size_t ll = strlen(lib);
        for (p = line; (p = strstr(p, lib)); p += ll) {
            int before = p == line ? 0 : (unsigned char)p[-1];
            int after = (unsigned char)p[ll];
            if (!is_word_char(before) && !is_word_char(after))
                return 1;
        }
    }
    return 0;
}

static void scan_build_file(scan_result *res, const char *rel, const char *src, size_t len)
{
    line_table lt;
    lines_build(&lt, src, len);
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    int is_cmake = !strcmp(base, "CMakeLists.txt") || str_ends_with(base, ".cmake");
    if (is_cmake)
        res->facts.has_cmake = 1;
    else if (!strcmp(base, "meson.build"))
        res->facts.has_meson = 1;
    else if (!strcmp(base, "configure.ac") || !strcmp(base, "configure.in") ||
             !strcmp(base, "Makefile.am"))
        res->facts.has_autotools = 1;
    else
        res->facts.has_make = 1;

    for (size_t ln = 1; ln <= lt.n; ln++) {
        char *text = line_text(&lt, (int)ln);
        if (text[0] == '#' || text[0] == '\0') {
            free(text);
            continue;
        }
        if (mentions_lib(text, "socket"))
            res->facts.links_socket = 1;
        if (mentions_lib(text, "regex"))
            res->facts.links_regex = 1;
        if (mentions_lib(text, "fsnotify"))
            res->facts.links_fsnotify = 1;
        if (is_cmake && strstr(text, "CMAKE_SYSTEM_NAME") && strstr(text, "Linux")) {
            const rule *ru = rule_by_id("QNX072");
            add_finding(res, ru, ru->sev, rel, (int)ln,
                        (int)(strstr(text, "CMAKE_SYSTEM_NAME") - text) + 1,
                        "CMAKE_SYSTEM_NAME Linux", text, 0);
        } else if (strstr(text, "if(LINUX)") || strstr(text, "if (LINUX)")) {
            const rule *ru = rule_by_id("QNX072");
            add_finding(res, ru, ru->sev, rel, (int)ln, 1, "if(LINUX)", text, 0);
        }
        {
            const char *u = strstr(text, "uname");
            if (u && (u == text || !is_word_char((unsigned char)u[-1])) &&
                !is_word_char((unsigned char)u[5])) {
                const rule *ru = rule_by_id("QNX073");
                add_finding(res, ru, ru->sev, rel, (int)ln, (int)(u - text) + 1,
                            "uname", text, 0);
            }
        }
        for (size_t r = 0; r < qport_rule_count; r++) {
            const rule *ru = &qport_rules[r];
            if (ru->kind != RK_BUILD_TEXT)
                continue;
            const char *p = ru->pattern;
            while (*p) {
                const char *e = strchr(p, '|');
                size_t n = e ? (size_t)(e - p) : strlen(p);
                char *pat = xstrndup(p, n);
                const char *hit = text;
                while ((hit = strstr(hit, pat))) {
                    int before = hit == text ? ' ' : (unsigned char)hit[-1];
                    int after = (unsigned char)hit[n];
                    int flag = pat[0] == '-';
                    if (!flag || (!is_word_char(before) && !is_word_char(after))) {
                        add_finding(res, ru, ru->sev, rel, (int)ln,
                                    (int)(hit - text) + 1, pat, text, 0);
                        break;
                    }
                    hit += n;
                }
                free(pat);
                if (!e)
                    break;
                p = e + 1;
            }
        }
        free(text);
    }
    free(lt.start);
}

/* ------------------------------------------------------------------ */
/* walking                                                            */
/* ------------------------------------------------------------------ */

static const char *const source_ext[] = {
    ".c", ".h", ".cc", ".cpp", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h++",
    ".ipp", ".inl", ".tcc", NULL
};

static int is_source(const char *name)
{
    for (const char *const *e = source_ext; *e; e++)
        if (str_ends_with(name, *e))
            return 1;
    return 0;
}

static int is_header(const char *name)
{
    return str_ends_with(name, ".h") || str_ends_with(name, ".hpp") ||
           str_ends_with(name, ".hh") || str_ends_with(name, ".hxx") ||
           str_ends_with(name, ".inl");
}

static int is_build(const char *name)
{
    return !strcmp(name, "Makefile") || !strcmp(name, "makefile") ||
           !strcmp(name, "GNUmakefile") || str_ends_with(name, ".mk") ||
           !strcmp(name, "CMakeLists.txt") || str_ends_with(name, ".cmake") ||
           !strcmp(name, "meson.build") || !strcmp(name, "configure.ac") ||
           !strcmp(name, "configure.in") || !strcmp(name, "Makefile.am");
}

typedef struct {
    char **v;
    size_t n, cap;
} path_list;

static void pl_add(path_list *pl, char *p)
{
    if (pl->n == pl->cap) {
        pl->cap = pl->cap ? pl->cap * 2 : 64;
        pl->v = xrealloc(pl->v, pl->cap * sizeof *pl->v);
    }
    pl->v[pl->n++] = p;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int excluded_dir(const char *name, const scan_options *opt)
{
    if (name[0] == '.')
        return 1;
    for (size_t i = 0; i < opt->n_excludes; i++)
        if (!strcmp(name, opt->excludes[i]))
            return 1;
    return 0;
}

static void walk(const char *dir, const char *rel, const scan_options *opt,
                 path_list *out, int depth)
{
    if (depth > 64)
        return;
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "qport: cannot open %s: %s\n", dir, strerror(errno));
        return;
    }
    struct dirent *de;
    while ((de = readdir(d))) {
        const char *name = de->d_name;
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;
        size_t pl = strlen(dir) + strlen(name) + 2;
        char *full = xmalloc(pl);
        snprintf(full, pl, "%s/%s", dir, name);
        size_t rl = strlen(rel) + strlen(name) + 2;
        char *r = xmalloc(rl);
        if (rel[0])
            snprintf(r, rl, "%s/%s", rel, name);
        else
            snprintf(r, rl, "%s", name);
        struct stat st;
        if (lstat(full, &st) != 0) {
            free(full);
            free(r);
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (!excluded_dir(name, opt))
                walk(full, r, opt, out, depth + 1);
            free(r);
        } else if (S_ISREG(st.st_mode) && (is_source(name) || is_build(name))) {
            pl_add(out, r);
        } else {
            free(r);
        }
        free(full);
    }
    closedir(d);
}

static void scan_one(scan_result *res, const scan_options *opt, const char *full, const char *rel)
{
    size_t len;
    char *src = read_file(full, &len);
    if (!src) {
        fprintf(stderr, "qport: cannot read %s: %s\n", full, strerror(errno));
        return;
    }
    const char *base = strrchr(rel, '/');
    base = base ? base + 1 : rel;
    res->files_scanned++;
    for (size_t i = 0; i < len; i++)
        if (src[i] == '\n')
            res->lines_scanned++;

    if (is_build(base)) {
        res->build_files++;
        scan_build_file(res, rel, src, len);
        free(src);
        return;
    }
    res->source_files++;
    if (strstr(src, "qport:ignore-file")) {
        free(src);
        return;
    }
    file_ctx fc;
    memset(&fc, 0, sizeof fc);
    fc.res = res;
    fc.opt = opt;
    fc.rel = rel;
    lines_build(&fc.lines, src, len);
    lex_source(src, len, &fc.tl);
    scan_source(&fc);
    tokens_free(&fc.tl);
    free(fc.lines.start);
    free(fc.cs.v);
    free(fc.forks);
    free(src);
}

static void link_checks(scan_result *res)
{
    struct {
        int uses, links;
        location *loc;
        const char *id, *what;
    } checks[] = {
        { res->facts.uses_sockets, res->facts.links_socket, &first_socket_use, "QNX080", "-lsocket" },
        { res->facts.uses_regex, res->facts.links_regex, &first_regex_use, "QNX081", "-lregex" },
        { res->facts.uses_inotify, res->facts.links_fsnotify, &first_inotify_use, "QNX082", "-lfsnotify" },
    };
    int any_build = res->facts.has_cmake || res->facts.has_make ||
                    res->facts.has_meson || res->facts.has_autotools;
    if (!any_build)
        return; /* nothing to check against; the summary still lists the libs */
    for (size_t i = 0; i < sizeof checks / sizeof checks[0]; i++) {
        if (checks[i].uses && !checks[i].links && checks[i].loc->file) {
            const rule *r = rule_by_id(checks[i].id);
            add_finding(res, r, r->sev, checks[i].loc->file, checks[i].loc->line,
                        checks[i].loc->col, checks[i].what, checks[i].loc->snippet, 0);
        }
    }
}

int scan_path(const char *path, const scan_options *opt, scan_result *res)
{
    memset(res, 0, sizeof *res);
    memset(&first_socket_use, 0, sizeof first_socket_use);
    memset(&first_regex_use, 0, sizeof first_regex_use);
    memset(&first_inotify_use, 0, sizeof first_inotify_use);
    res->root = xstrdup(path);

    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "qport: %s: %s\n", path, strerror(errno));
        return -1;
    }
    path_list files = { 0 };
    if (S_ISDIR(st.st_mode)) {
        walk(path, "", opt, &files, 0);
        qsort(files.v, files.n, sizeof *files.v, cmp_str);
    } else {
        const char *base = strrchr(path, '/');
        pl_add(&files, xstrdup(base ? base + 1 : path));
    }

    /* project headers first, so includes of our own files are not flagged */
    for (size_t i = 0; i < files.n; i++) {
        if (is_header(files.v[i])) {
            if (res->n_headers == res->cap_headers) {
                res->cap_headers = res->cap_headers ? res->cap_headers * 2 : 64;
                res->headers = xrealloc(res->headers, res->cap_headers * sizeof *res->headers);
            }
            res->headers[res->n_headers++] = xstrdup(files.v[i]);
        }
    }

    /* pre-pass: integer macros and struct layouts, headers first */
    for (int hdr_pass = 1; hdr_pass >= 0; hdr_pass--) {
        for (size_t i = 0; i < files.n; i++) {
            const char *bn = strrchr(files.v[i], '/');
            bn = bn ? bn + 1 : files.v[i];
            if (is_build(bn) || is_header(bn) != hdr_pass)
                continue;
            char *full;
            if (S_ISDIR(st.st_mode)) {
                size_t n = strlen(path) + strlen(files.v[i]) + 2;
                full = xmalloc(n);
                snprintf(full, n, "%s/%s", path, files.v[i]);
            } else {
                full = xstrdup(path);
            }
            size_t len;
            char *src = read_file(full, &len);
            if (src) {
                token_list tl = { 0 };
                lex_source(src, len, &tl);
                learn_definitions(&tl);
                tokens_free(&tl);
                free(src);
            }
            free(full);
        }
    }

    for (size_t i = 0; i < files.n; i++) {
        char *full;
        if (S_ISDIR(st.st_mode)) {
            size_t n = strlen(path) + strlen(files.v[i]) + 2;
            full = xmalloc(n);
            snprintf(full, n, "%s/%s", path, files.v[i]);
        } else {
            full = xstrdup(path);
        }
        if (opt->progress) {
            int w = 28, done = (int)((i + 1) * (size_t)w / files.n);
            fprintf(stderr, "\r\033[2K  scanning  [%.*s%*s]  %zu/%zu  %.40s",
                    done, "############################", w - done, "",
                    i + 1, files.n, files.v[i]);
            fflush(stderr);
        }
        scan_one(res, opt, full, files.v[i]);
        free(full);
        free(files.v[i]);
    }
    if (opt->progress && files.n) {
        fputs("\r\033[2K", stderr);
        fflush(stderr);
    }
    free(files.v);
    forget_definitions();
    link_checks(res);
    scan_sort(res);
    return 0;
}

static int cmp_finding(const void *a, const void *b)
{
    const finding *x = a, *y = b;
    int c = strcmp(x->file, y->file);
    if (c)
        return c;
    if (x->line != y->line)
        return x->line - y->line;
    if (x->col != y->col)
        return x->col - y->col;
    return strcmp(x->r->id, y->r->id);
}

void scan_sort(scan_result *res)
{
    qsort(res->findings.v, res->findings.n, sizeof *res->findings.v, cmp_finding);
}

size_t count_sev(const scan_result *res, severity s, int include_guarded)
{
    size_t n = 0;
    for (size_t i = 0; i < res->findings.n; i++) {
        const finding *f = &res->findings.v[i];
        if (f->guarded && !include_guarded)
            continue;
        if (f->sev == s)
            n++;
    }
    return n;
}

void scan_result_free(scan_result *res)
{
    for (size_t i = 0; i < res->findings.n; i++) {
        free(res->findings.v[i].file);
        free(res->findings.v[i].what);
        free(res->findings.v[i].snippet);
    }
    free(res->findings.v);
    for (size_t i = 0; i < res->n_headers; i++)
        free(res->headers[i]);
    free(res->headers);
    free(res->root);
    memset(res, 0, sizeof *res);
}
