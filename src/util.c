/* SPDX-License-Identifier: MIT */
#include "qport.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void oom(void)
{
    fputs("qport: out of memory\n", stderr);
    exit(3);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        oom();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        oom();
    return q;
}

char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

char *xstrndup(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

void sb_init(strbuf *sb)
{
    sb->cap  = 256;
    sb->len  = 0;
    sb->data = xmalloc(sb->cap);
    sb->data[0] = '\0';
}

void sb_free(strbuf *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

static void sb_grow(strbuf *sb, size_t extra)
{
    if (sb->len + extra + 1 <= sb->cap)
        return;
    while (sb->len + extra + 1 > sb->cap)
        sb->cap *= 2;
    sb->data = xrealloc(sb->data, sb->cap);
}

void sb_putn(strbuf *sb, const char *s, size_t n)
{
    sb_grow(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_putc(strbuf *sb, char c)
{
    sb_putn(sb, &c, 1);
}

void sb_puts(strbuf *sb, const char *s)
{
    sb_putn(sb, s, strlen(s));
}

void sb_printf(strbuf *sb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    sb_grow(sb, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sb->len += (size_t)n;
}

void sb_json_str(strbuf *sb, const char *s)
{
    sb_putc(sb, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  sb_puts(sb, "\\\""); break;
        case '\\': sb_puts(sb, "\\\\"); break;
        case '\n': sb_puts(sb, "\\n");  break;
        case '\r': sb_puts(sb, "\\r");  break;
        case '\t': sb_puts(sb, "\\t");  break;
        default:
            if (*p < 0x20)
                sb_printf(sb, "\\u%04x", *p);
            else
                sb_putc(sb, (char)*p);
        }
    }
    sb_putc(sb, '"');
}

char *read_file(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    strbuf sb;
    sb_init(&sb);
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        sb_putn(&sb, buf, n);
    int err = ferror(f);
    fclose(f);
    if (err) {
        sb_free(&sb);
        return NULL;
    }
    if (len_out)
        *len_out = sb.len;
    return sb.data;
}

int str_ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

int str_starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}
