/* SPDX-License-Identifier: MIT */
#include "qport.h"

#include <stdio.h>
#include <string.h>

#define C_RESET  "\033[0m"
#define C_BOLD   "\033[1m"
#define C_DIM    "\033[2m"
#define C_RED    "\033[31m"
#define C_GREEN  "\033[32m"
#define C_YELLOW "\033[33m"
#define C_CYAN   "\033[36m"

static const char *sev_color(severity s)
{
    return s == SEV_ERROR ? C_RED : s == SEV_WARN ? C_YELLOW : C_CYAN;
}

/* Word-wrap `text` at `width`, indenting continuation lines. */
static void wrap(strbuf *out, const char *text, int indent, int width, int first_used)
{
    int col = first_used;
    const char *p = text;
    while (*p) {
        const char *w = p;
        while (*w && *w != ' ')
            w++;
        int wl = (int)(w - p);
        if (col > indent && col + 1 + wl > width) {
            sb_putc(out, '\n');
            for (int i = 0; i < indent; i++)
                sb_putc(out, ' ');
            col = indent;
        } else if (col > indent || (col == indent && p != text)) {
            sb_putc(out, ' ');
            col++;
        }
        sb_putn(out, p, (size_t)wl);
        col += wl;
        p = w;
        while (*p == ' ')
            p++;
    }
}

static int visible(const finding *f, int show_guarded)
{
    return !f->guarded || show_guarded;
}

static void libs_needed(const scan_result *res, strbuf *out)
{
    const build_facts *bf = &res->facts;
    int any = 0;
    if (bf->uses_sockets) { sb_puts(out, " -lsocket"); any = 1; }
    if (bf->uses_regex)   { sb_puts(out, " -lregex"); any = 1; }
    if (bf->uses_inotify) { sb_puts(out, " -lfsnotify"); any = 1; }
    if (!any)
        sb_puts(out, " (libc only)");
}

static void write_text(const scan_result *res, int color, int show_guarded, strbuf *out)
{
    const char *B = color ? C_BOLD : "", *D = color ? C_DIM : "", *R = color ? C_RESET : "";
    sb_printf(out, "%sqport %s%s  QNX OS 8.0 portability scan of %s\n", B, QPORT_VERSION, R, res->root);
    sb_printf(out, "%s%zu source files, %zu build files, %zu lines%s\n\n", D,
              res->source_files, res->build_files, res->lines_scanned, R);

    const char *cur = NULL;
    for (size_t i = 0; i < res->findings.n; i++) {
        const finding *f = &res->findings.v[i];
        if (!visible(f, show_guarded))
            continue;
        if (!cur || strcmp(cur, f->file) != 0) {
            cur = f->file;
            sb_printf(out, "%s%s%s\n", B, f->file, R);
        }
        const char *sc = color ? sev_color(f->sev) : "";
        char loc[32];
        snprintf(loc, sizeof loc, "%d:%d", f->line, f->col);
        sb_printf(out, "  %-7s %s%-7s%s %s%s%s  ", loc, sc, sev_name(f->sev), R, D, f->r->id, R);
        strbuf head;
        sb_init(&head);
        sb_printf(&head, "%s: %s%s", f->what, f->r->title,
                  f->guarded ? " [in a branch QNX does not compile]" : "");
        wrap(out, head.data, 26, 100, 26);
        sb_free(&head);
        sb_putc(out, '\n');
        if (f->snippet[0])
            sb_printf(out, "%26s%s| %s%s\n", "", D, f->snippet, R);
        sb_printf(out, "%26s%sfix:%s ", "", color ? C_GREEN : "", R);
        wrap(out, f->r->fix, 31, 100, 31);
        sb_putc(out, '\n');
    }
    if (cur)
        sb_putc(out, '\n');

    size_t e = count_sev(res, SEV_ERROR, show_guarded);
    size_t w = count_sev(res, SEV_WARN, show_guarded);
    size_t n = count_sev(res, SEV_NOTE, show_guarded);
    size_t hidden = 0;
    for (size_t i = 0; i < res->findings.n; i++)
        if (res->findings.v[i].guarded && !show_guarded)
            hidden++;

    sb_printf(out, "%sSummary:%s %s%zu error%s%s, %s%zu warning%s%s, %s%zu note%s%s",
              B, R, color && e ? C_RED : "", e, e == 1 ? "" : "s", R,
              color && w ? C_YELLOW : "", w, w == 1 ? "" : "s", R,
              color && n ? C_CYAN : "", n, n == 1 ? "" : "s", R);
    if (hidden)
        sb_printf(out, "  %s(%zu more inside #if branches QNX does not compile; --show-guarded)%s",
                  D, hidden, R);
    sb_putc(out, '\n');

    /* most frequent rules: where the porting effort goes */
    size_t counts[128] = { 0 };
    for (size_t i = 0; i < res->findings.n; i++) {
        const finding *f = &res->findings.v[i];
        if (!visible(f, show_guarded))
            continue;
        size_t idx = (size_t)(f->r - qport_rules);
        if (idx < 128)
            counts[idx]++;
    }
    int shown = 0;
    for (int round = 0; round < 6; round++) {
        size_t best = 0, bi = 0;
        for (size_t k = 0; k < qport_rule_count && k < 128; k++)
            if (counts[k] > best) {
                best = counts[k];
                bi = k;
            }
        if (!best)
            break;
        if (!shown)
            sb_printf(out, "%sTop rules:%s ", B, R);
        else
            sb_puts(out, ", ");
        sb_printf(out, "%s x%zu", qport_rules[bi].id, best);
        counts[bi] = 0;
        shown++;
    }
    if (shown)
        sb_putc(out, '\n');
    sb_printf(out, "%sLink on QNX:%s", B, R);
    libs_needed(res, out);
    sb_putc(out, '\n');
    if (e)
        sb_printf(out, "%sVerdict:%s %sneeds porting%s: %zu blocking change%s before this builds for QNX 8.0.\n",
                  B, R, color ? C_RED : "", R, e, e == 1 ? "" : "s");
    else if (w)
        sb_printf(out, "%sVerdict:%s %slikely to build%s; review %zu warning%s for behaviour on QNX 8.0.\n",
                  B, R, color ? C_YELLOW : "", R, w, w == 1 ? "" : "s");
    else
        sb_printf(out, "%sVerdict:%s %sno blockers found%s for QNX 8.0.\n", B, R, color ? C_GREEN : "", R);
}

static size_t count_guarded(const scan_result *res)
{
    size_t g = 0;
    for (size_t i = 0; i < res->findings.n; i++)
        g += res->findings.v[i].guarded ? 1 : 0;
    return g;
}

static void write_brief(const scan_result *res, int color, int show_guarded, strbuf *out)
{
    const char *D = color ? C_DIM : "", *R = color ? C_RESET : "";
    for (size_t i = 0; i < res->findings.n; i++) {
        const finding *f = &res->findings.v[i];
        if (!visible(f, show_guarded))
            continue;
        sb_printf(out, "%s:%d:%d: %s%s%s %s%s%s %s: %s\n", f->file, f->line, f->col,
                  color ? sev_color(f->sev) : "", sev_name(f->sev), R, D, f->r->id, R,
                  f->what, f->r->title);
    }
}

static void write_json(const scan_result *res, int show_guarded, strbuf *out)
{
    sb_puts(out, "{\n  \"tool\": \"qport\",\n  \"version\": ");
    sb_json_str(out, QPORT_VERSION);
    sb_puts(out, ",\n  \"target\": \"QNX OS 8.0\",\n  \"root\": ");
    sb_json_str(out, res->root);
    sb_printf(out, ",\n  \"files\": {\"source\": %zu, \"build\": %zu, \"lines\": %zu},\n",
              res->source_files, res->build_files, res->lines_scanned);
    sb_printf(out, "  \"summary\": {\"error\": %zu, \"warning\": %zu, \"note\": %zu, \"guarded\": %zu},\n",
              count_sev(res, SEV_ERROR, show_guarded), count_sev(res, SEV_WARN, show_guarded),
              count_sev(res, SEV_NOTE, show_guarded),
              count_guarded(res));
    sb_puts(out, "  \"qnx_link_libs\": [");
    int first = 1;
    if (res->facts.uses_sockets) { sb_puts(out, "\"socket\""); first = 0; }
    if (res->facts.uses_regex)   { sb_puts(out, first ? "\"regex\"" : ", \"regex\""); first = 0; }
    if (res->facts.uses_inotify) { sb_puts(out, first ? "\"fsnotify\"" : ", \"fsnotify\""); }
    sb_puts(out, "],\n  \"findings\": [");
    first = 1;
    for (size_t i = 0; i < res->findings.n; i++) {
        const finding *f = &res->findings.v[i];
        if (!visible(f, show_guarded))
            continue;
        sb_puts(out, first ? "\n    {" : ",\n    {");
        first = 0;
        sb_puts(out, "\"rule\": ");
        sb_json_str(out, f->r->id);
        sb_puts(out, ", \"severity\": ");
        sb_json_str(out, sev_name(f->sev));
        sb_puts(out, ", \"file\": ");
        sb_json_str(out, f->file);
        sb_printf(out, ", \"line\": %d, \"col\": %d, \"what\": ", f->line, f->col);
        sb_json_str(out, f->what);
        sb_printf(out, ", \"guarded\": %s, \"title\": ", f->guarded ? "true" : "false");
        sb_json_str(out, f->r->title);
        sb_puts(out, ", \"fix\": ");
        sb_json_str(out, f->r->fix);
        sb_puts(out, ", \"snippet\": ");
        sb_json_str(out, f->snippet);
        sb_putc(out, '}');
    }
    sb_puts(out, first ? "]\n}\n" : "\n  ]\n}\n");
}

static const char *sarif_level(severity s)
{
    return s == SEV_ERROR ? "error" : s == SEV_WARN ? "warning" : "note";
}

static void write_sarif(const scan_result *res, int show_guarded, strbuf *out)
{
    sb_puts(out, "{\n  \"$schema\": \"https://json.schemastore.org/sarif-2.1.0.json\",\n"
                 "  \"version\": \"2.1.0\",\n  \"runs\": [{\n"
                 "    \"tool\": {\"driver\": {\"name\": \"qport\", \"version\": ");
    sb_json_str(out, QPORT_VERSION);
    sb_puts(out, ", \"rules\": [");
    for (size_t i = 0; i < qport_rule_count; i++) {
        const rule *r = &qport_rules[i];
        sb_puts(out, i ? ",\n      {" : "\n      {");
        sb_puts(out, "\"id\": ");
        sb_json_str(out, r->id);
        sb_puts(out, ", \"shortDescription\": {\"text\": ");
        sb_json_str(out, r->title);
        sb_puts(out, "}, \"help\": {\"text\": ");
        strbuf h;
        sb_init(&h);
        sb_printf(&h, "%s (Source: %s)", r->fix, r->ref);
        sb_json_str(out, h.data);
        sb_free(&h);
        sb_printf(out, "}, \"defaultConfiguration\": {\"level\": \"%s\"}}", sarif_level(r->sev));
    }
    sb_puts(out, "\n    ]}},\n    \"results\": [");
    int first = 1;
    for (size_t i = 0; i < res->findings.n; i++) {
        const finding *f = &res->findings.v[i];
        if (!visible(f, show_guarded))
            continue;
        sb_puts(out, first ? "\n      {" : ",\n      {");
        first = 0;
        sb_puts(out, "\"ruleId\": ");
        sb_json_str(out, f->r->id);
        sb_printf(out, ", \"level\": \"%s\", \"message\": {\"text\": ", sarif_level(f->sev));
        strbuf m;
        sb_init(&m);
        sb_printf(&m, "%s: %s. Fix for QNX 8.0: %s", f->what, f->r->title, f->r->fix);
        sb_json_str(out, m.data);
        sb_free(&m);
        sb_puts(out, "}, \"locations\": [{\"physicalLocation\": {\"artifactLocation\": {\"uri\": ");
        sb_json_str(out, f->file);
        sb_printf(out, "}, \"region\": {\"startLine\": %d, \"startColumn\": %d}}}]}",
                  f->line, f->col > 0 ? f->col : 1);
    }
    sb_puts(out, first ? "]\n  }]\n}\n" : "\n    ]\n  }]\n}\n");
}

static void md_cell(strbuf *out, const char *s)
{
    for (; *s; s++) {
        if (*s == '|')
            sb_puts(out, "\\|");
        else if (*s == '\n')
            sb_putc(out, ' ');
        else
            sb_putc(out, *s);
    }
}

static void write_markdown(const scan_result *res, int show_guarded, strbuf *out)
{
    sb_printf(out, "# QNX OS 8.0 portability report: `%s`\n\n", res->root);
    sb_printf(out, "Scanned %zu source files and %zu build files (%zu lines) with qport %s.\n\n",
              res->source_files, res->build_files, res->lines_scanned, QPORT_VERSION);
    sb_printf(out, "| Errors | Warnings | Notes |\n|---|---|---|\n| %zu | %zu | %zu |\n\n",
              count_sev(res, SEV_ERROR, show_guarded), count_sev(res, SEV_WARN, show_guarded),
              count_sev(res, SEV_NOTE, show_guarded));
    sb_puts(out, "Link on QNX:");
    libs_needed(res, out);
    sb_puts(out, "\n\n| Severity | Rule | Location | Finding | Fix on QNX 8.0 |\n|---|---|---|---|---|\n");
    for (size_t i = 0; i < res->findings.n; i++) {
        const finding *f = &res->findings.v[i];
        if (!visible(f, show_guarded))
            continue;
        sb_printf(out, "| %s | %s | `%s:%d` | `", sev_name(f->sev), f->r->id, f->file, f->line);
        md_cell(out, f->what);
        sb_puts(out, "` ");
        md_cell(out, f->r->title);
        sb_puts(out, " | ");
        md_cell(out, f->r->fix);
        sb_puts(out, " |\n");
    }
}

void report_write(const scan_result *res, out_format fmt, int color, int show_guarded, strbuf *out)
{
    switch (fmt) {
    case FMT_JSON:     write_json(res, show_guarded, out); break;
    case FMT_SARIF:    write_sarif(res, show_guarded, out); break;
    case FMT_MARKDOWN: write_markdown(res, show_guarded, out); break;
    case FMT_BRIEF:    write_brief(res, color, show_guarded, out); break;
    default:           write_text(res, color, show_guarded, out); break;
    }
}

void report_rules(int color, strbuf *out)
{
    const char *R = color ? C_RESET : "";
    for (size_t i = 0; i < qport_rule_count; i++) {
        const rule *r = &qport_rules[i];
        sb_printf(out, "%s  %s%-7s%s  %s\n", r->id, color ? sev_color(r->sev) : "",
                  sev_name(r->sev), R, r->title);
    }
    sb_printf(out, "\n%zu rules. Details: qport explain <ID>\n", qport_rule_count);
}

int report_explain(const char *id, strbuf *out)
{
    const rule *r = rule_by_id(id);
    if (!r)
        return -1;
    sb_printf(out, "%s (%s)\n%s\n\n", r->id, sev_name(r->sev), r->title);
    sb_puts(out, "Matches: ");
    for (const char *p = r->pattern; *p; p++)
        sb_puts(out, *p == '|' ? ", " : (char[]){ *p, 0 });
    sb_puts(out, "\n\nOn QNX 8.0: ");
    wrap(out, r->fix, 0, 78, 12);
    sb_printf(out, "\n\nSource: %s\n", r->ref);
    return 0;
}
