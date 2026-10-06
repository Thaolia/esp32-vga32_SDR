#pragma once
#include <math.h>
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(fabsf((float)(a) - (float)(b)) <= (tol))
#define RUN(t) do { if (t()) { fprintf(stderr, "FAIL %s\n", #t); failures++; } else printf("ok %s\n", #t); } while (0)
