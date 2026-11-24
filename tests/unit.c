/* Unit tests for the tokenizer and the rule table. */
#include "qport.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static token_list lex(const char *s)
{
    token_list tl = { 0 };
    lex_source(s, strlen(s), &tl);
    return tl;
}

static void test_comments_and_strings(void)
{
    token_list tl = lex("a /* b */ c // d\n\"e // f\" 'g' h");
    CHECK(tl.n == 5);
    CHECK(tl.v[0].kind == TK_IDENT && !strcmp(tl.v[0].text, "a"));
    CHECK(tl.v[1].kind == TK_IDENT && !strcmp(tl.v[1].text, "c"));
    CHECK(tl.v[2].kind == TK_STRING && !strcmp(tl.v[2].text, "e // f"));
    CHECK(tl.v[3].kind == TK_CHAR);
    CHECK(tl.v[4].kind == TK_IDENT && tl.v[4].line == 2);
    tokens_free(&tl);
}

static void test_directives(void)
{
    token_list tl = lex("  #  include <sys/epoll.h> // x\n#define A \\\n  42\nint x;");
    CHECK(tl.n == 5);
    CHECK(tl.v[0].kind == TK_PP && !strcmp(tl.v[0].text, "include <sys/epoll.h>"));
    CHECK(tl.v[1].kind == TK_PP && strstr(tl.v[1].text, "define A") && strstr(tl.v[1].text, "42"));
    CHECK(tl.v[2].kind == TK_IDENT && tl.v[2].line == 4);
    tokens_free(&tl);
}

static void test_hash_not_at_line_start(void)
{
    token_list tl = lex("x = a # b;");
    for (size_t i = 0; i < tl.n; i++)
        CHECK(tl.v[i].kind != TK_PP);
    tokens_free(&tl);
}

static void test_raw_string(void)
{
    token_list tl = lex("auto s = R\"x(epoll_wait())\" )x\"; y");
    int saw_y = 0;
    for (size_t i = 0; i < tl.n; i++) {
        CHECK(!(tl.v[i].kind == TK_IDENT && !strcmp(tl.v[i].text, "epoll_wait")));
        if (tl.v[i].kind == TK_IDENT && !strcmp(tl.v[i].text, "y"))
            saw_y = 1;
    }
    CHECK(saw_y);
    tokens_free(&tl);
}

static void test_numbers(void)
{
    token_list tl = lex("0x1000 1'000'000 1e+5 64u");
    CHECK(tl.n == 4);
    CHECK(!strcmp(tl.v[1].text, "1'000'000"));
    CHECK(!strcmp(tl.v[2].text, "1e+5"));
    tokens_free(&tl);
}

static void test_rules_table(void)
{
    CHECK(qport_rule_count > 40);
    for (size_t i = 0; i < qport_rule_count; i++) {
        const rule *r = &qport_rules[i];
        CHECK(r->id && strncmp(r->id, "QNX", 3) == 0);
        CHECK(r->title && r->title[0]);
        CHECK(r->fix && r->fix[0]);
        CHECK(r->ref && r->ref[0]);
        for (size_t j = i + 1; j < qport_rule_count; j++)
            CHECK(strcmp(r->id, qport_rules[j].id) != 0);
    }
    CHECK(rule_by_id("QNX020") != NULL);
    CHECK(rule_by_id("NOPE") == NULL);
}

int main(void)
{
    test_comments_and_strings();
    test_directives();
    test_hash_not_at_line_start();
    test_raw_string();
    test_numbers();
    test_rules_table();
    if (failures) {
        fprintf(stderr, "%d unit check(s) failed\n", failures);
        return 1;
    }
    printf("unit: all checks passed\n");
    return 0;
}
