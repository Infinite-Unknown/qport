#include <stdint.h>

#define FRAMES   64
#define FRAME_SZ (16 * 1024)

struct pkt {
    uint64_t ts;
    uint16_t id;
    float    v;
};

static char file_scope[1 << 20];          /* not on the stack */

void work(int n)
{
    char small[256];                       /* fine */
    static char kept[1 << 22];             /* static: not on the stack */
    unsigned char frames[FRAMES][FRAME_SZ];/* 1 MiB via macros: flagged */
    struct pkt ring[8192];                 /* 8192 * 16 = 128 KiB: flagged */
    double grid[100][100];                 /* 80000 bytes = 78 KiB: flagged */
    char vla[n];                           /* VLA: note */
    int ok[1024];                          /* 4 KiB: fine */
    small[0] = vla[0] = (char)ok[0];
    (void)frames; (void)ring; (void)grid; (void)kept;
}
