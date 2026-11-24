#include <stdio.h>          /* in the sysroot */
#include <sys/neutrino.h>   /* in the sysroot */
#include <proj/api.h>       /* in the project */
#include <openssl/ssl.h>    /* missing: must be ported */
#include "local.h"          /* quoted: never checked */

int main(void) { return api(); }
