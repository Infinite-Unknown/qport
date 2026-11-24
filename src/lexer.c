/* SPDX-License-Identifier: MIT
 *
 * A forgiving C/C++ tokenizer. It is not a compiler front end: it only needs
 * to tell identifiers, literals and preprocessor directives apart so that
 * rules never fire inside comments or strings.
 */
#include "qport.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *s;
    size_t      len;
    size_t      i;
    int         line;
    int         col;
    int         at_line_start; /* only whitespace seen since the last newline */
} lexer;

static int peek(const lexer *lx, size_t ahead)
{
    size_t j = lx->i + ahead;
    return j < lx->len ? (unsigned char)lx->s[j] : -1;
}

static void advance(lexer *lx)
{
    if (lx->i >= lx->len)
        return;
    if (lx->s[lx->i] == '\n') {
        lx->line++;
        lx->col = 1;
        lx->at_line_start = 1;
    } else {
        lx->col++;
    }
    lx->i++;
}

static void push(token_list *tl, tok_kind k, char *text, int line, int col)
{
    if (tl->n == tl->cap) {
        tl->cap = tl->cap ? tl->cap * 2 : 256;
        tl->v = xrealloc(tl->v, tl->cap * sizeof *tl->v);
    }
    tl->v[tl->n].kind = k;
    tl->v[tl->n].text = text;
    tl->v[tl->n].line = line;
    tl->v[tl->n].col  = col;
    tl->n++;
}

static int is_ident_start(int c)
{
    return c == '_' || c == '$' || (c >= 0 && c < 128 && isalpha(c));
}

static int is_ident_char(int c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

/* Skip a // or block comment starting at the current position. */
static int skip_comment(lexer *lx)
{
    if (peek(lx, 0) == '/' && peek(lx, 1) == '/') {
        while (lx->i < lx->len && lx->s[lx->i] != '\n') {
            /* a backslash-newline continues a // comment */
            if (lx->s[lx->i] == '\\' && peek(lx, 1) == '\n')
                advance(lx);
            advance(lx);
        }
        return 1;
    }
    if (peek(lx, 0) == '/' && peek(lx, 1) == '*') {
        advance(lx);
        advance(lx);
        while (lx->i < lx->len && !(peek(lx, 0) == '*' && peek(lx, 1) == '/'))
            advance(lx);
        advance(lx);
        advance(lx);
        return 1;
    }
    return 0;
}

/* Read a quoted literal; returns its contents (escapes kept verbatim). */
static char *read_quoted(lexer *lx, char q)
{
    strbuf sb;
    sb_init(&sb);
    advance(lx); /* opening quote */
    while (lx->i < lx->len) {
        int c = peek(lx, 0);
        if (c == '\\' && lx->i + 1 < lx->len) {
            if (peek(lx, 1) == '\n') { /* line continuation inside literal */
                advance(lx);
                advance(lx);
                continue;
            }
            sb_putc(&sb, (char)c);
            advance(lx);
            sb_putc(&sb, (char)peek(lx, 0));
            advance(lx);
            continue;
        }
        if (c == q) {
            advance(lx);
            break;
        }
        if (c == '\n') /* unterminated; stop at end of line */
            break;
        sb_putc(&sb, (char)c);
        advance(lx);
    }
    return sb.data;
}

/* C++11 raw string R"delim( ... )delim" */
static char *read_raw_string(lexer *lx)
{
    advance(lx); /* R */
    advance(lx); /* " */
    strbuf delim;
    sb_init(&delim);
    while (lx->i < lx->len && peek(lx, 0) != '(' && delim.len < 16) {
        sb_putc(&delim, (char)peek(lx, 0));
        advance(lx);
    }
    advance(lx); /* ( */
    strbuf body;
    sb_init(&body);
    while (lx->i < lx->len) {
        if (peek(lx, 0) == ')' &&
            lx->i + 1 + delim.len < lx->len &&
            memcmp(lx->s + lx->i + 1, delim.data, delim.len) == 0 &&
            lx->s[lx->i + 1 + delim.len] == '"') {
            for (size_t k = 0; k < delim.len + 2; k++)
                advance(lx);
            break;
        }
        sb_putc(&body, (char)peek(lx, 0));
        advance(lx);
    }
    sb_free(&delim);
    return body.data;
}

/* A preprocessor directive: collect the logical line, drop comments. */
static char *read_directive(lexer *lx)
{
    strbuf sb;
    sb_init(&sb);
    advance(lx); /* '#' */
    while (lx->i < lx->len) {
        int c = peek(lx, 0);
        if (c == '\\' && peek(lx, 1) == '\n') {
            advance(lx);
            advance(lx);
            sb_putc(&sb, ' ');
            continue;
        }
        if (c == '\\' && peek(lx, 1) == '\r' && peek(lx, 2) == '\n') {
            advance(lx);
            advance(lx);
            advance(lx);
            sb_putc(&sb, ' ');
            continue;
        }
        if (c == '\n')
            break;
        if (c == '/' && (peek(lx, 1) == '/' || peek(lx, 1) == '*')) {
            int was_line = peek(lx, 1) == '/';
            if (was_line) {
                while (lx->i < lx->len && peek(lx, 0) != '\n')
                    advance(lx);
                break;
            }
            skip_comment(lx);
            sb_putc(&sb, ' ');
            continue;
        }
        if (c == '"' || c == '\'') {
            /* keep literals (e.g. #include "x.h") but do not end on '//' inside them */
            char q = (char)c;
            sb_putc(&sb, q);
            advance(lx);
            while (lx->i < lx->len && peek(lx, 0) != q && peek(lx, 0) != '\n') {
                if (peek(lx, 0) == '\\' && peek(lx, 1) != '\n') {
                    sb_putc(&sb, '\\');
                    advance(lx);
                }
                sb_putc(&sb, (char)peek(lx, 0));
                advance(lx);
            }
            if (peek(lx, 0) == q) {
                sb_putc(&sb, q);
                advance(lx);
            }
            continue;
        }
        if (c != '\r')
            sb_putc(&sb, (char)c);
        advance(lx);
    }
    /* trim */
    size_t a = 0;
    while (a < sb.len && isspace((unsigned char)sb.data[a]))
        a++;
    size_t b = sb.len;
    while (b > a && isspace((unsigned char)sb.data[b - 1]))
        b--;
    char *out = xstrndup(sb.data + a, b - a);
    sb_free(&sb);
    return out;
}

void lex_source(const char *src, size_t len, token_list *out)
{
    lexer lx = { src, len, 0, 1, 1, 1 };
    while (lx.i < lx.len) {
        int c = peek(&lx, 0);
        if (c == '\n') {
            advance(&lx);
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            advance(&lx);
            continue;
        }
        if (c == '\\' && peek(&lx, 1) == '\n') { /* stray continuation */
            advance(&lx);
            advance(&lx);
            continue;
        }
        if (c == '/' && (peek(&lx, 1) == '/' || peek(&lx, 1) == '*')) {
            int keep = lx.at_line_start;
            skip_comment(&lx);
            lx.at_line_start = keep || lx.at_line_start;
            continue;
        }
        int line = lx.line, col = lx.col;
        if (c == '#' && lx.at_line_start) {
            char *d = read_directive(&lx);
            push(out, TK_PP, d, line, col);
            continue;
        }
        lx.at_line_start = 0;
        if ((c == 'R' && peek(&lx, 1) == '"') ||
            ((c == 'u' || c == 'U' || c == 'L') && peek(&lx, 1) == 'R' && peek(&lx, 2) == '"')) {
            if (c != 'R')
                advance(&lx);
            push(out, TK_STRING, read_raw_string(&lx), line, col);
            continue;
        }
        if (c == '"') {
            push(out, TK_STRING, read_quoted(&lx, '"'), line, col);
            continue;
        }
        if (c == '\'') {
            push(out, TK_CHAR, read_quoted(&lx, '\''), line, col);
            continue;
        }
        if (is_ident_start(c)) {
            size_t start = lx.i;
            while (lx.i < lx.len && is_ident_char(peek(&lx, 0)))
                advance(&lx);
            /* string prefixes: u8"", L"", u"", U"" */
            size_t n = lx.i - start;
            if (peek(&lx, 0) == '"' &&
                ((n == 1 && strchr("LuU", src[start])) ||
                 (n == 2 && src[start] == 'u' && src[start + 1] == '8'))) {
                push(out, TK_STRING, read_quoted(&lx, '"'), line, col);
                continue;
            }
            push(out, TK_IDENT, xstrndup(src + start, n), line, col);
            continue;
        }
        if (c >= '0' && c <= '9') {
            size_t start = lx.i;
            while (lx.i < lx.len) {
                int d = peek(&lx, 0);
                if (is_ident_char(d) || d == '.' || d == '\'')
                    advance(&lx);
                else if ((d == '+' || d == '-') && lx.i > start &&
                         strchr("eEpP", src[lx.i - 1]))
                    advance(&lx);
                else
                    break;
            }
            push(out, TK_NUMBER, xstrndup(src + start, lx.i - start), line, col);
            continue;
        }
        char p[2] = { (char)c, 0 };
        advance(&lx);
        push(out, TK_PUNCT, xstrdup(p), line, col);
    }
}

void tokens_free(token_list *tl)
{
    for (size_t i = 0; i < tl->n; i++)
        free(tl->v[i].text);
    free(tl->v);
    tl->v = NULL;
    tl->n = tl->cap = 0;
}
