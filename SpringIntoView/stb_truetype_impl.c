#include "../pmm.h" // For pmm_alloc
#include "../string.h" // For memcpy, memset, strlen

// Provide memory allocation functions for stb_truetype
#define STBTT_malloc(x,u) ((void)(u), pmm_alloc(x))
#define STBTT_free(x,u)   ((void)(u)) // pmm_alloc can't free
#define STBTT_realloc(p,n,u) ((void)(u), pmm_alloc(n)) // Simplistic realloc

// Provide string functions for stb_truetype
#define STBTT_memcpy(dest, src, size) memcpy(dest, src, size)
#define STBTT_memset(ptr, value, size) memset(ptr, value, size)
#define STBTT_strlen(str) strlen(str)

// Define necessary math functions to avoid including math.h for stb_truetype
#define STBTT_ifloor(x) ((int)(x))
#define STBTT_iceil(x) ((int)((x) + 0.999f))
#define STBTT_sqrt(x) (0) // Not needed for our use case, returning 0
#define STBTT_pow(x,y) (0) // Not needed for our use case, returning 0
#define STBTT_fmod(x,y) ((x) - (y) * STBTT_ifloor((x)/(y)))
#define STBTT_fabs(x) ((x) < 0 ? -(x) : (x))
#define STBTT_acos(x) (0) // Not needed for our use case, returning 0
#define STBTT_cos(x) (0) // Not needed for our use case, returning 0
#define STBTT_assert(x) ((void)0)

// Embed the stb_truetype implementation
#define STB_TRUETYPE_IMPLEMENTATION
#include "../libs/stb_truetype.h" 