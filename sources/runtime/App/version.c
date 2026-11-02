
#ifdef VERSION_STRING
#define VER " " VERSION_STRING
#else
#define VER ""
#endif

#ifdef ENABLE_FEAT_F4HWN
/* Keep the product identity in the device-info reply.  UVStudio and the
     * multiboot metadata scanner use this string to identify Falcon images. */
const char Version[] = AUTHOR_STRING_2 " " VERSION_STRING_2;
const char Edition[] = EDITION_STRING;
#else
const char Version[] = AUTHOR_STRING VER;
#endif

const char UART_Version[] = "FALCON\r\n";

_Static_assert(sizeof(Version) <= 16, "Serial firmware identity must fit the 16-byte reply");
