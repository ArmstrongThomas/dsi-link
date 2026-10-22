/* DSi Link wire format, shared by the DSi receiver and the host tests.
 *
 * The sender connects to TCP LINK_PORT and sends a 20-byte little-endian header:
 *   0  'D' 'S' 'L' 'K'
 *   4  u32 flags (LINK_FLAG_RUN boots the file after saving)
 *   8  u32 file size in bytes
 *   12 u32 CRC-32 of the file (zlib polynomial)
 *   16 u16 name length, then u16 zero
 * followed by the file name, then, after the receiver answers "READY\n", the file bytes.
 * The receiver answers each request with one line: "OK SAVED\n", "OK SAVED, BOOTING\n"
 * or "ERR <reason>\n". */
#ifndef LINK_PROTOCOL_H
#define LINK_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LINK_VERSION "1.1"
#define LINK_PORT 17494
#define LINK_HEAD_BYTES 20u
#define LINK_NAME_MAX 63u
#define LINK_MAX_SIZE (32u*1024u*1024u)
#define LINK_FLAG_RUN 1u

typedef struct { uint32_t flags,size,crc32; uint16_t name_length; } LinkHeader;

static inline uint32_t link_get32(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}

static inline bool link_parse_header(const unsigned char *p,LinkHeader *h){
    if(memcmp(p,"DSLK",4)||p[18]||p[19])return false;
    h->flags=link_get32(p+4);h->size=link_get32(p+8);h->crc32=link_get32(p+12);h->name_length=(uint16_t)(p[16]|(p[17]<<8));
    return h->name_length>0&&h->name_length<=LINK_NAME_MAX&&!(h->flags&~LINK_FLAG_RUN);
}

/* Accepts a plain file name ending in .nds: letters, digits, space, dot, dash and underscore. */
static inline bool link_valid_name(const char *s,unsigned n,char out[LINK_NAME_MAX+1]){
    if(n<5||n>LINK_NAME_MAX||s[0]=='.'||s[0]==' ')return false;
    for(unsigned i=0;i<n;i++){char c=s[i];bool ok=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==' '||c=='.'||c=='-'||c=='_';if(!ok)return false;if(c=='.'&&i+1<n&&s[i+1]=='.')return false;}
    const char *ext=s+n-4;if(ext[0]!='.'||(ext[1]|32)!='n'||(ext[2]|32)!='d'||(ext[3]|32)!='s')return false;
    memcpy(out,s,n);out[n]=0;return true;
}

/* zlib-compatible CRC-32 using a 16-entry nibble table. */
static inline uint32_t link_crc32(uint32_t crc,const unsigned char *p,size_t n){
    static const uint32_t t[16]={0x00000000u,0x1DB71064u,0x3B6E20C8u,0x26D930ACu,0x76DC4190u,0x6B6B51F4u,0x4DB26158u,0x5005713Cu,
        0xEDB88320u,0xF00F9344u,0xD6D6A3E8u,0xCB61B38Cu,0x9B64C2B0u,0x86D3D2D4u,0xA00AE278u,0xBDBDF21Cu};
    crc=~crc;for(size_t i=0;i<n;i++){crc^=p[i];crc=(crc>>4)^t[crc&15];crc=(crc>>4)^t[crc&15];}return ~crc;
}
#endif
