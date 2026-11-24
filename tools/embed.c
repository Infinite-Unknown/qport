/* Turns report/template.html into src/html_template.c (a C string).
 * Build-time helper: cc -o tools/embed tools/embed.c */
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: embed <template.html> <out.c>\n");
        return 2;
    }
    FILE *in = fopen(argv[1], "rb"), *out = fopen(argv[2], "w");
    if (!in || !out) {
        perror("embed");
        return 1;
    }
    fputs("/* SPDX-License-Identifier: MIT\n * Generated from report/template.html by tools/embed.c. Do not edit.\n"
          " * qport:ignore-file\n */\n#include \"qport.h\"\n\nconst char qport_html_template[] = {", out);
    int c;
    unsigned long n = 0;
    while ((c = fgetc(in)) != EOF)
        fprintf(out, "%s%d,", n++ % 24 ? "" : "\n    ", c);
    fputs("\n    0\n};\n", out);
    fclose(in);
    return fclose(out) == 0 ? 0 : 1;
}
