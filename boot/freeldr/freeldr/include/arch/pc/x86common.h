
#ifndef HEX
#define HEX(y) 0x##y
#endif

/* Memory layout */

#define BIOSCALLBUFFER      HEX(4000) /* Buffer to store temporary data for any Int386() call */
#define BIOSCALLBUFSEGMENT (BIOSCALLBUFFER/16)
#define BIOSCALLBUFOFFSET   HEX(0000)
#define BIOSCALLBUFSIZE     PAGE_SIZE /* max is sizeof(VESA_SVGA_INFO) = 512 */

#define STACKLOW            HEX(7000)
#define STACKADDR           HEX(F000) /* The 32/64-bit stack top will be at 0000:F000, or 0xF000 */

#define FREELDR_BASE        HEX(F800)
#define FREELDR_PE_BASE     HEX(10000)

#define MEMORY_MARGIN      HEX(88000) /* We need this much memory */
#define MAX_FREELDR_PE_SIZE (MEMORY_MARGIN - FREELDR_PE_BASE - PAGE_SIZE)

/* MAX_DISKREADBUFFER_SIZE is later passed to INT 13h, AH=42h.
   According to https://en.wikipedia.org/wiki/INT_13H#INT_13h_AH.3D42h:_Extended_Read_Sectors_From_Drive
   some BIOSes can only read a maximum of 127 sectors. (0xFE00 / 512 = 127)
   Confirmed when booting from USB on Dell Latitude D531 and Lenovo ThinkPad X61. */
#define MAX_DISKREADBUFFER_SIZE HEX(FE00)
