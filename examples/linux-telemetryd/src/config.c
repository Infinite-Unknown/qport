/* Config: which signals to publish, reloaded when the file changes. */
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>

#include "telemetryd.h"

static regex_t filter;
static int have_filter;

int config_watch(const char *path)
{
    int fd = inotify_init1(IN_NONBLOCK);
    inotify_add_watch(fd, path, IN_CLOSE_WRITE);
    return fd;
}

int config_reload(const char *path)
{
    char pattern[256] = "";
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    if (fgets(pattern, sizeof pattern, f))
        pattern[strcspn(pattern, "\n")] = '\0';
    fclose(f);
    if (have_filter)
        regfree(&filter);
    have_filter = regcomp(&filter, pattern, REG_EXTENDED | REG_NOSUB) == 0;
    return have_filter ? 0 : -1;
}

int config_signal_enabled(const char *name)
{
    return !have_filter || regexec(&filter, name, 0, NULL, 0) == 0;
}
