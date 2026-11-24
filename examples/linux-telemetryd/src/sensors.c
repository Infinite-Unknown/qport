/* Sensor sources: board thermal zones and the CAN gateway's shared ring. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <unistd.h>

#include "telemetryd.h"

static int ring_id = -1;
static float *ring;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static float read_thermal(int zone)
{
    char path[64], buf[32];
    snprintf(path, sizeof path, "/sys/class/thermal/thermal_zone%d/temp", zone);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1.0f;
    float v = fgets(buf, sizeof buf, f) ? strtof(buf, NULL) / 1000.0f : -1.0f;
    fclose(f);
    return v;
}

int sensors_open(void)
{
    ring_id = shmget(0x7e1e, MAX_SIGNALS * sizeof(float), 0444);
    if (ring_id >= 0)
        ring = shmat(ring_id, NULL, SHM_RDONLY);
    int ncpu = get_nprocs();
    fprintf(stderr, "telemetryd: %d CPUs online\n", ncpu);
    return ring_id;
}

int sensors_read(struct sample *out, int max)
{
    int n = 0;
    uint64_t t = now_ns();
    for (int z = 0; z < 2 && n < max; z++)
        out[n++] = (struct sample){ t, (uint16_t)(1000 + z), read_thermal(z) };
    for (int i = 0; ring && i < MAX_SIGNALS - 2 && n < max; i++)
        out[n++] = (struct sample){ t, (uint16_t)i, ring[i] };
    return n;
}

long free_memory_kb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char line[128];
    long kb = -1;
    while (f && fgets(line, sizeof line, f))
        if (sscanf(line, "MemAvailable: %ld kB", &kb) == 1)
            break;
    if (f)
        fclose(f);
    return kb;
}
