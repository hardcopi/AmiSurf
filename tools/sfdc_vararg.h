/* GCC 16 / AmigaDev NDK: Roadshow inline/bsdsocket.h uses _sfdc_vararg
 * without the typedef sfdc puts in other *_protos.h headers. */
#if !defined(_SFDC_VARARG_DEFINED) && !defined(__ASSEMBLER__)
#define _SFDC_VARARG_DEFINED
typedef unsigned long _sfdc_vararg;
#endif
