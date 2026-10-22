/* Host checks for the DSi Link wire format.
 *   link_check                         self-test of header parsing, name rules and CRC-32
 *   link_check capture payload name    verifies bytes a sender produced against the receiver parser */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../source/link_protocol.h"

/* Bitwise reference, independent of the receiver's nibble-table version. */
static uint32_t reference_crc32(const unsigned char *p,size_t n){
    uint32_t c=0xFFFFFFFFu;for(size_t i=0;i<n;i++){c^=p[i];for(int k=0;k<8;k++)c=(c&1u)?0xEDB88320u^(c>>1):c>>1;}return ~c;
}
static unsigned char *read_all(const char *path,long *n){
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char *b=malloc((size_t)*n+1);assert(b&&fread(b,1,(size_t)*n,f)==(size_t)*n);fclose(f);return b;
}
static void self_test(void){
    const unsigned char nine[]="123456789";assert(link_crc32(0,nine,9)==0xCBF43926u);
    unsigned char buf[4099];for(unsigned i=0;i<sizeof(buf);i++)buf[i]=(unsigned char)(i*131u+17u);
    assert(link_crc32(0,buf,sizeof(buf))==reference_crc32(buf,sizeof(buf)));
    assert(link_crc32(link_crc32(0,buf,100),buf+100,sizeof(buf)-100)==link_crc32(0,buf,sizeof(buf)));
    unsigned char h[20]={'D','S','L','K',1,0,0,0,0x10,0,0,0,0x78,0x56,0x34,0x12,15,0,0,0};LinkHeader r;
    assert(link_parse_header(h,&r)&&r.flags==LINK_FLAG_RUN&&r.size==16&&r.crc32==0x12345678u&&r.name_length==15);
    h[4]=2;assert(!link_parse_header(h,&r));h[4]=1;
    h[18]=1;assert(!link_parse_header(h,&r));h[18]=0;
    h[16]=0;assert(!link_parse_header(h,&r));h[16]=64;assert(!link_parse_header(h,&r));h[16]=15;
    h[0]='X';assert(!link_parse_header(h,&r));
    char out[LINK_NAME_MAX+1];
    assert(link_valid_name("PixelDoodle.nds",15,out)&&!strcmp(out,"PixelDoodle.nds"));
    assert(link_valid_name("my build_2-b.NDS",16,out));
    const char *bad[]={"../x.nds","a/b.nds","a\\b.nds",".hidden.nds"," lead.nds","game.nd","game.nds.bak","fat:x.nds","a..b.nds","tab\t.nds"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!link_valid_name(bad[i],(unsigned)strlen(bad[i]),out));
    char name[80];memset(name,'a',60);memcpy(name+60,".nds",5);assert(!link_valid_name(name,64,out));
    memcpy(name+59,".nds",5);assert(link_valid_name(name,63,out));
    puts("Header parsing, file-name rules and CRC-32: PASS");
}
int main(int argc,char **argv){
    if(argc==4){
        long hn,pn;unsigned char *h=read_all(argv[1],&hn),*p=read_all(argv[2],&pn);LinkHeader r;char out[LINK_NAME_MAX+1];
        assert(hn>=20&&link_parse_header(h,&r)&&hn==20+(long)r.name_length);
        assert(link_valid_name((const char *)h+20,r.name_length,out)&&!strcmp(out,argv[3]));
        assert(r.size==(uint32_t)pn&&r.crc32==link_crc32(0,p,(size_t)pn)&&r.crc32==reference_crc32(p,(size_t)pn));
        free(h);free(p);printf("Sender bytes for %s match the receiver parser: PASS\n",argv[3]);return 0;
    }
    self_test();return 0;
}
