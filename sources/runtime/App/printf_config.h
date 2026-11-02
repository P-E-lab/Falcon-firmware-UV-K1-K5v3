#define PRINTF_DISABLE_SUPPORT_LONG_LONG
#define PRINTF_DISABLE_SUPPORT_EXPONENTIAL
#define PRINTF_DISABLE_SUPPORT_PTRDIFF_T
#define PRINTF_DISABLE_SUPPORT_FLOAT

/* All active formats verified by Format-inventory.json; keep decimal, octal, strings and padding. */
#if !defined(DEBUG)
#define PRINTF_USE_MINIMAL
#endif
