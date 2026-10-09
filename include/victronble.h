/* Deprecated: the core header was renamed to victronble_core.h in 0.7.1.
 * This forwarder keeps 0.7.0 Zephyr and host code building. It lives only in
 * include/, never in src/, where it would clash with VictronBLE.h on
 * case-insensitive filesystems (issue #2). */
#ifndef VICTRONBLE_H
#define VICTRONBLE_H
#include "victronble_core.h"
#endif
