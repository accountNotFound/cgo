#pragma once

#include <stdio.h>

#ifdef DEBUG
#undef DEBUG
#endif

#ifdef USE_DEBUG
#undef USE_DEBUG
#define DEBUG(fmt, ...) printf("%s -> %s() [line: %d] " fmt "\n", __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUG(fmt, ...)
#endif