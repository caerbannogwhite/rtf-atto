#ifndef RTF_ABI_H
#define RTF_ABI_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
  RTF_OK = 0,
  RTF_ERR_NOT_RTF = -1,           /* the input does not start with {\rtf */
  RTF_ERR_INVALID_ARGUMENT = -2,  /* out or out_len is null, or data is null with size > 0 */
  RTF_ERR_OUT_OF_MEMORY = -3      /* malloc failed for the output */
};

/* Parses size bytes at data. On RTF_OK, *out is a malloc'd buffer holding the
   canonical JSON of the document (UTF-8, *out_len bytes, followed by one NUL
   byte that is not counted). On a negative code, *out holds a short message
   the same way ("not_rtf"), except RTF_ERR_OUT_OF_MEMORY and
   RTF_ERR_INVALID_ARGUMENT, which store NULL and 0 when they can. The caller
   frees *out with rtf_free. data may be NULL when size is 0. */
int rtf_parse(const unsigned char* data, size_t size, unsigned char** out, size_t* out_len);

/* The same contract; the JSON is the array of logical tables. */
int rtf_logical_tables(const unsigned char* data, size_t size, unsigned char** out, size_t* out_len);

/* Frees a buffer returned in *out. Accepts NULL. */
void rtf_free(void* p);

/* The version string of the core, static storage, NUL-terminated. */
const char* rtf_version(void);

#ifdef __cplusplus
}
#endif
#endif
