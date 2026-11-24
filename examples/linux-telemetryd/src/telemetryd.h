#ifndef TELEMETRYD_H
#define TELEMETRYD_H

#include <stdint.h>

#define MAX_SIGNALS 64

struct sample {
    uint64_t t_ns;
    uint16_t signal_id;
    float    value;
};

int  sensors_open(void);
int  sensors_read(struct sample *out, int max);
int  publish_open(const char *host, const char *port);
int  publish_batch(int sock, const struct sample *s, int n);
int  config_watch(const char *path);
int  config_reload(const char *path);
int  config_signal_enabled(const char *name);

#endif
