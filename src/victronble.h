/* Arduino include-path shim: Arduino builds only add src/ to the include
 * path, so route to the canonical core header in include/. Zephyr and host
 * builds add include/ directly and never see this file first — both paths
 * end up in the same header (it has an include guard). */
#include "../include/victronble.h"
