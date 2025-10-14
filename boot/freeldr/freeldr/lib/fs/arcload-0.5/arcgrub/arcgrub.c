#include <grub/arcgrub.h>
#include <stdarg.h>

grub_err_t grub_errno = 0;

grub_uint16_t grub_le_to_cpu16(grub_uint16_t x)
{
	return (x<<8)|(x>>8);
}
grub_uint32_t grub_le_to_cpu32(grub_uint32_t x)
{
	x=((x<<8)&0xFF00FF00)|((x>>8)&0x00FF00FF);
	return (x<<16)|(x>>16);
}
grub_uint64_t grub_le_to_cpu64(grub_uint64_t x)
{
	x=((x<<8)&0xFF00FF00FF00FF00ULL)|((x>>8)&0x00FF00FF00FF00FFULL);
	x=((x<<16)&0xFFFF0000FFFF0000ULL)|((x>>16)&0x0000FFFF0000FFFFULL);
	return (x<<32)|(x>>32);
}

grub_uint8_t *grub_utf16_to_utf8 (grub_uint8_t *dest, grub_uint16_t *src,
		    grub_size_t size)
{
  grub_uint32_t code_high = 0;

  while (size--) {
      grub_uint32_t code = *src++;
      if (code_high) {
	  if (code >= 0xDC00 && code <= 0xDFFF) {
	      /* Surrogate pair.  */
	      code = ((code_high - 0xD800) << 12) + (code - 0xDC00) + 0x10000;
	      *dest++ = (code >> 18) | 0xF0;
	      *dest++ = ((code >> 12) & 0x3F) | 0x80;
	      *dest++ = ((code >> 6) & 0x3F) | 0x80;
	      *dest++ = (code & 0x3F) | 0x80;
	  } else {
	      /* Error...  */
	      *dest++ = '?';
	  }
	  code_high = 0;
      } else {
	  if (code <= 0x007F)
	    *dest++ = code;
	  else if (code <= 0x07FF) {
	      *dest++ = (code >> 6) | 0xC0;
	      *dest++ = (code & 0x3F) | 0x80;
	  } else if (code >= 0xD800 && code <= 0xDBFF) {
	      code_high = code;
	      continue;
	  } else if (code >= 0xDC00 && code <= 0xDFFF) {
	      /* Error... */
	      *dest++ = '?';
	  } else {
	      *dest++ = (code >> 16) | 0xE0;
	      *dest++ = ((code >> 12) & 0x3F) | 0x80;
	      *dest++ = (code & 0x3F) | 0x80;
	  }
      }
  }
  return dest;
}

char grub_tolower(char x)
{
	if(x>'A' && x<'Z')
		return x+'a'-'A';
	return x;
}

int grub_isspace(char x)
{
	switch(x) {
	case ' ':
	case '\f':
	case '\n':
	case '\r':
	case '\t':
	case '\v':
		return 1;
	}
	return 0;
}

static char *gruberrs[]={
	"NONE",
	"TEST_FAILURE",
	"BAD_MODULE",
	"OUT_OF_MEMORY",
	"BAD_FILE_TYPE",
	"FILE_NOT_FOUND",
	"FILE_READ_ERROR",
	"BAD_FILENAME",
	"UNKNOWN_FS",
	"BAD_FS",
	"BAD_NUMBER",
	"OUT_OF_RANGE",
	"UNKNOWN_DEVICE",
	"BAD_DEVICE",
	"READ_ERROR",
	"WRITE_ERROR",
	"UNKNOWN_COMMAND",
	"INVALID_COMMAND",
	"BAD_ARGUMENT",
	"BAD_PART_TABLE",
	"UNKNOWN_OS",
	"BAD_OS",
	"NO_KERNEL",
	"BAD_FONT",
	"NOT_IMPLEMENTED_YET",
	"SYMLINK_LOOP",
	"BAD_GZIP_DATA",
};

grub_err_t grub_error(grub_err_t n, const char *fmt, ...)
{
	va_list ap;
	printf("GRUB Filesystem Error %s: '", gruberrs[n]);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	printf("'\r\n");
	grub_errno = n;
	return n;
}
