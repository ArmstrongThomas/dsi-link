/* DSi Link: receive an .nds over Wi-Fi, save it to Homebrew/Dev, then boot it through Pico Loader.
 * Exiting the booted app returns here, so a build can be sent, tested and replaced without
 * removing the SD card. Runs in DSi mode so DSWiFi can join WPA/WPA2 Advanced Setup networks. */
#include <nds.h>
#include <dswifi9.h>
#include <fat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "link_protocol.h"

/* Received builds go to their own folder so a send never replaces a working copy in Homebrew/Dev. */
#define TARGET_DIR "fat:/Homebrew/Dev/Received"
#define CHUNK 16384u
#define RECV_PER_FRAME 4u
#define TIMEOUT_FRAMES (60u*10u)

/* Header layout of picoLoader7.bin, transcribed from Pico Loader's include/picoLoader7.h
 * (LNH team, Zlib licence), API version 3. Fields are never written with byte stores. */
typedef struct {
    char romPath[256];
    char savePath[256];
    u32 argumentsLength;
    char arguments[256];
} PloadParams;
typedef struct {
    void *const entryPoint;
    void *dldiDriver;
    u16 bootDrive;
    const u16 apiVersion;
    PloadParams loadParams;
    char launcherPath[256];      /* API version 2 */
    const void *cheats;          /* API version 3 */
} PloadHeader7;
#define PLOAD_BOOT_DRIVE_DLDI 0
#define PICO_LOADER9 ((volatile u16 *)0x06800000)
#define PICO_LOADER7 ((volatile u16 *)0x06840000)

extern char _io_dldi_stub[];

enum { S_WIFI_INIT, S_WIFI_WAIT, S_LISTEN, S_HEAD, S_BODY, S_IDLE_ERROR };

static int stage=S_WIFI_INIT, listen_fd=-1, client_fd=-1;
static unsigned quiet_frames;
static unsigned char head[LINK_HEAD_BYTES+LINK_NAME_MAX];
static unsigned head_got,head_need;
static LinkHeader request;
static char name[LINK_NAME_MAX+1], self_path[256], last_rom[128], address_line[40];
static FILE *part;
static u32 got,crc;
static unsigned char *chunk;

static void status(const char *s){printf("\x1b[2;0H%-32.32s",s);}
static void detail(const char *s){printf("\x1b[3;0H%-32.32s",s);}
/* Rows 8-19 hold the log; the title, address and return path above it stay put. */
static void log_line(const char *s){static int row=8;if(row>19){for(int r=8;r<=19;r++)printf("\x1b[%d;0H%32s",r,"");row=8;}printf("\x1b[%d;0H%-32.32s",row++,s);}

static void join(char *out,size_t n,const char *dir,const char *file,const char *suffix){snprintf(out,n,"%s/%s%s",dir,file,suffix);}
static bool exists(const char *p){struct stat st;return stat(p,&st)==0;}
static void close_client(void){if(client_fd>=0){close(client_fd);client_fd=-1;}}
static void reply(const char *line){if(client_fd>=0){size_t n=strlen(line);send(client_fd,line,n,0);}}
static void fail(const char *why){
    char line[96];snprintf(line,sizeof(line),"ERR %s\n",why);reply(line);log_line(why);
    if(part){fclose(part);part=NULL;char p[128];join(p,sizeof(p),TARGET_DIR,name,".part");remove(p);}
    close_client();stage=S_LISTEN;status("WAITING FOR A BUILD");
}
static bool set_nonblocking(int fd){int on=1;return ioctl(fd,FIONBIO,&on)==0;}

static bool start_listening(void){
    listen_fd=socket(AF_INET,SOCK_STREAM,0);if(listen_fd<0)return false;
    struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(LINK_PORT);a.sin_addr.s_addr=htonl(INADDR_ANY);
    if(bind(listen_fd,(struct sockaddr *)&a,sizeof(a))<0||listen(listen_fd,1)<0||!set_nonblocking(listen_fd)){close(listen_fd);listen_fd=-1;return false;}
    struct in_addr ip={.s_addr=Wifi_GetIP()};snprintf(address_line,sizeof(address_line),"%s : %u",inet_ntoa(ip),LINK_PORT);
    printf("\x1b[5;0H%-32.32s",address_line);return true;
}

/* Keeps the previous copy as .nds.bak until the new file is in place. */
static bool install(void){
    char path[128],bak[128],tmp[128];join(path,sizeof(path),TARGET_DIR,name,"");join(bak,sizeof(bak),TARGET_DIR,name,".bak");join(tmp,sizeof(tmp),TARGET_DIR,name,".part");
    remove(bak);
    if(exists(path)&&rename(path,bak)!=0)return false;
    if(rename(tmp,path)!=0){rename(bak,path);return false;}
    snprintf(last_rom,sizeof(last_rom),"%s",path);return true;
}

static void copy16(volatile u16 *dst,const void *src,u32 bytes){const u16 *s=src;for(u32 i=0;i<(bytes+1)/2;i++)dst[i]=s[i];}
static unsigned char *load(const char *path,u32 *size,u32 max){
    FILE *f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char *b=(n>0&&(u32)n<=max)?malloc((size_t)n+1):NULL;if(b&&fread(b,1,(size_t)n,f)!=(size_t)n){free(b);b=NULL;}fclose(f);if(b)*size=(u32)n;return b;
}
/* Pico Loader paths drop the "fat:" device prefix. */
static const char *loader_path(const char *p){return strncmp(p,"fat:",4)==0?p+4:p;}

/* MPU off so the ARM9 can execute picoLoader9 from VRAM; caches were flushed by the caller. */
__attribute__((target("arm"),noinline,noreturn)) static void enter_loader9(void){
    asm volatile("mrc p15,0,r0,c1,c0,0\n bic r0,r0,#0x1000\n bic r0,r0,#0x5\n mcr p15,0,r0,c1,c0,0\n ldr r0,=0x06800000\n bx r0\n":::"r0","memory");
    __builtin_unreachable();
}

static void draw_screen(void){
    consoleClear();printf("\x1b[0;0HDSi Link %s",LINK_VERSION);printf("\x1b[20;0HA: BOOT LAST  START: EXIT");
    if(address_line[0])printf("\x1b[5;0H%-32.32s",address_line);
    printf("\x1b[6;0HEXITING AN APP RETURNS TO:\x1b[7;0H%-32.32s",self_path[0]?loader_path(self_path):"NOWHERE (NOT FOUND)");
}
static void printable(char *s){for(;*s;s++)if(*s<32||*s>126)*s='?';}
/* Shows what is about to be handed to Pico Loader and waits for A (boot) or B (cancel). */
static bool boot_check(const char *rom,const unsigned char *l7){
    const unsigned char *d=(const unsigned char *)_io_dldi_stub;u32 magic=(u32)d[0]|((u32)d[1]<<8)|((u32)d[2]<<16)|((u32)d[3]<<24);
    char driver[49],type[5];memcpy(driver,d+0x10,48);driver[48]=0;memcpy(type,d+0x60,4);type[4]=0;printable(driver);printable(type);
    u32 entry=(u32)l7[0]|((u32)l7[1]<<8)|((u32)l7[2]<<16)|((u32)l7[3]<<24);unsigned api=(unsigned)(l7[10]|(l7[11]<<8));
    consoleClear();printf("\x1b[0;0HBOOT CHECK");
    printf("\x1b[2;0HCARD DRIVER (DLDI)");printf("\x1b[3;0H%-32.32s",driver);
    printf("\x1b[4;0HTYPE %s  MAGIC %s",type,magic==0xBF8DA5EDu?"OK":"BAD");printf("\x1b[5;0HSIZE 2^%u  AT %08lX",d[0x0D],(unsigned long)(u32)_io_dldi_stub);
    printf("\x1b[7;0HPICO LOADER API %u",api);printf("\x1b[8;0HARM7 ENTRY %08lX",(unsigned long)entry);
    printf("\x1b[10;0HROM:");printf("\x1b[11;0H%-32.32s",loader_path(rom));
    printf("\x1b[13;0HRETURN:");printf("\x1b[14;0H%-32.32s",self_path[0]?loader_path(self_path):"NONE");
    printf("\x1b[16;0HMODE %s",isDSiMode()?"DSI":"DS");printf("\x1b[20;0HA: BOOT       B: CANCEL     ");
    for(;;){cothread_yield_irq(IRQ_VBLANK);scanKeys();unsigned k=keysDown();if(k&KEY_A)return true;if(k&KEY_B)return false;}
}

static void boot(const char *rom){
    u32 n9=0,n7=0;unsigned char *l9=load("fat:/_pico/picoLoader9.bin",&n9,256u*1024u),*l7=load("fat:/_pico/picoLoader7.bin",&n7,256u*1024u);
    if(!l9||!l7){free(l9);free(l7);log_line("PICO LOADER FILES NOT FOUND");status("SAVED; LAUNCH IT FROM PICO LAUNCHER");return;}
    if(!boot_check(rom,l7)){free(l9);free(l7);draw_screen();status("BOOT CANCELLED");return;}
    static PloadParams params;memset(&params,0,sizeof(params));
    snprintf(params.romPath,sizeof(params.romPath),"%s",loader_path(rom));
    int n=snprintf(params.arguments,sizeof(params.arguments),"%s",rom);params.argumentsLength=(u32)n+1;
    static char launcher[256];memset(launcher,0,sizeof(launcher));snprintf(launcher,sizeof(launcher),"%s",loader_path(self_path));
    status("BOOTING");if(listen_fd>=0)close(listen_fd);Wifi_DisconnectAP();
    vramSetBankA(VRAM_A_LCD);vramSetBankB(VRAM_B_LCD);vramSetBankC(VRAM_C_LCD);vramSetBankD(VRAM_D_LCD);
    copy16(PICO_LOADER9,l9,n9);copy16(PICO_LOADER7,l7,n7);free(l9);free(l7);
    volatile PloadHeader7 *h=(volatile PloadHeader7 *)PICO_LOADER7;
    u16 api=h->apiVersion;u32 entry=(u32)h->entryPoint;
    h->dldiDriver=_io_dldi_stub;h->bootDrive=PLOAD_BOOT_DRIVE_DLDI;
    copy16((volatile u16 *)&h->loadParams,&params,sizeof(params));
    if(api>=2)copy16((volatile u16 *)h->launcherPath,launcher,sizeof(launcher));
    if(api>=3)h->cheats=NULL;
    REG_IME=0;for(int i=0;i<4;i++)DMA_CR(i)=0;if(isDSiMode())for(int i=0;i<4;i++)NDMA_CR(i)=0;
    DC_FlushAll();DC_InvalidateAll();IC_InvalidateAll();
    vramSetBankC(VRAM_C_ARM7_0x06000000);vramSetBankD(VRAM_D_ARM7_0x06020000);
    resetARM7(entry);
    enter_loader9();
}

static void receive_head(void){
    int n=recv(client_fd,head+head_got,head_need-head_got,0);
    if(n==0){fail("SENDER CLOSED EARLY");return;}
    if(n<0){if(errno!=EWOULDBLOCK&&errno!=EAGAIN)fail("NETWORK ERROR");else if(++quiet_frames>TIMEOUT_FRAMES)fail("SENDER TIMED OUT");return;}
    quiet_frames=0;head_got+=(unsigned)n;
    if(head_got==LINK_HEAD_BYTES&&head_need==LINK_HEAD_BYTES){
        if(!link_parse_header(head,&request)){fail("NOT A DSI LINK SENDER");return;}
        head_need=LINK_HEAD_BYTES+request.name_length;
    }
    if(head_got<head_need)return;
    if(!link_valid_name((const char *)head+LINK_HEAD_BYTES,request.name_length,name)){fail("FILE NAME MUST BE A PLAIN .NDS");return;}
    char target[128];join(target,sizeof(target),TARGET_DIR,name,"");if(!strcasecmp(target,self_path)){fail("CANNOT REPLACE DSI LINK ITSELF");return;}
    if(request.size==0||request.size>LINK_MAX_SIZE){fail("FILE SIZE OUT OF RANGE");return;}
    char tmp[128];join(tmp,sizeof(tmp),TARGET_DIR,name,".part");part=fopen(tmp,"wb");
    if(!part){fail("CANNOT WRITE TO THE CARD");return;}
    got=0;crc=0;stage=S_BODY;reply("READY\n");
    char line[48];snprintf(line,sizeof(line),"RECEIVING %.20s",name);status(line);
}

static void receive_body(void){
    for(unsigned i=0;i<RECV_PER_FRAME&&got<request.size;i++){
        u32 want=request.size-got;if(want>CHUNK)want=CHUNK;
        int n=recv(client_fd,chunk,want,0);
        if(n==0){fail("TRANSFER INTERRUPTED");return;}
        if(n<0){if(errno!=EWOULDBLOCK&&errno!=EAGAIN){fail("NETWORK ERROR");return;}if(++quiet_frames>TIMEOUT_FRAMES)fail("SENDER TIMED OUT");return;}
        quiet_frames=0;
        if(fwrite(chunk,1,(size_t)n,part)!=(size_t)n){fail("CARD WRITE FAILED");return;}
        crc=link_crc32(crc,chunk,(size_t)n);got+=(u32)n;
    }
    char line[40];snprintf(line,sizeof(line),"%lu / %lu KB",(unsigned long)(got/1024),(unsigned long)(request.size/1024));detail(line);
    if(got<request.size)return;
    bool ok=fflush(part)==0&&fsync(fileno(part))==0;if(fclose(part))ok=false;part=NULL;
    if(!ok){fail("CARD WRITE FAILED");return;}
    if(crc!=request.crc32){fail("CHECKSUM MISMATCH; RESEND");return;}
    if(!install()){fail("COULD NOT REPLACE THE OLD FILE");return;}
    char saved[64];snprintf(saved,sizeof(saved),"SAVED %.24s",name);log_line(saved);
    if(request.flags&LINK_FLAG_RUN){reply("OK SAVED, BOOTING\n");close_client();boot(last_rom);stage=S_LISTEN;return;}
    reply("OK SAVED\n");close_client();stage=S_LISTEN;status("WAITING FOR A BUILD");
}

/* Booted apps return to self_path. Use the path Pico Loader passes in argv; otherwise look in
 * the usual homebrew folders, then fall back to Pico Launcher itself. Empty means no return. */
static void resolve_return_path(int argc,char **argv){
    static const char *const places[]={"fat:/Homebrew/Dev/DSiLink.nds","fat:/Homebrew/DevTools/DSiLink.nds","fat:/Homebrew/DSiLink.nds","fat:/DSiLink.nds","fat:/_picoboot.nds"};
    self_path[0]=0;
    if(argc>0&&argv[0]&&!strncmp(argv[0],"fat:",4)&&exists(argv[0])){snprintf(self_path,sizeof(self_path),"%s",argv[0]);return;}
    for(unsigned i=0;i<sizeof(places)/sizeof(*places);i++)if(exists(places[i])){snprintf(self_path,sizeof(self_path),"%s",places[i]);return;}
}
int main(int argc,char **argv){
    consoleDemoInit();
    printf("\x1b[0;0HDSi Link %s",LINK_VERSION);
    printf("\x1b[20;0HA: BOOT LAST  START: EXIT");
    chunk=malloc(CHUNK);
    if(!chunk||!fatInitDefault()){status("SD CARD NOT AVAILABLE");for(;;){swiWaitForVBlank();scanKeys();if(keysDown()&KEY_START)return 0;}}
    resolve_return_path(argc,argv);
    draw_screen();
    mkdir("fat:/Homebrew",0777);mkdir("fat:/Homebrew/Dev",0777);mkdir(TARGET_DIR,0777);
    if(!isDSiMode())detail("DS MODE: OPEN/WEP NETWORKS ONLY");
    for(;;){
        cothread_yield_irq(IRQ_VBLANK);scanKeys();unsigned down=keysDown();
        if(stage!=S_BODY&&stage!=S_HEAD){
            if(down&KEY_START){close_client();if(listen_fd>=0)close(listen_fd);return 0;}
            if((down&KEY_A)&&last_rom[0])boot(last_rom);
        }
        switch(stage){
        case S_WIFI_INIT:
            status("STARTING WI-FI");
            /* DSi mode lets DSWiFi load Advanced Setup (WPA2) connections; DS hardware falls back. */
            if(!Wifi_InitDefault(INIT_ONLY|WIFI_ATTEMPT_DSI_MODE)){status("WI-FI FAILED TO START");stage=S_IDLE_ERROR;break;}
            if(Wifi_GetData(WIFIGETDATA_NUMWFCAPS,0,NULL)<=0){status("NO SAVED WI-FI CONNECTION");stage=S_IDLE_ERROR;break;}
            Wifi_AutoConnect();stage=S_WIFI_WAIT;status("CONNECTING TO WI-FI");break;
        case S_WIFI_WAIT:{
            int r=Wifi_AssocStatus();
            if(r==ASSOCSTATUS_CANNOTCONNECT){status("SAVED WI-FI CONNECTION FAILED");stage=S_IDLE_ERROR;break;}
            if(r==ASSOCSTATUS_ASSOCIATED&&Wifi_GetIP()){if(start_listening()){stage=S_LISTEN;status("WAITING FOR A BUILD");}else{status("COULD NOT OPEN PORT");stage=S_IDLE_ERROR;}}
            break;}
        case S_LISTEN:{
            int c=accept(listen_fd,NULL,NULL);
            if(c>=0){if(!set_nonblocking(c)){close(c);break;}client_fd=c;head_got=0;head_need=LINK_HEAD_BYTES;quiet_frames=0;stage=S_HEAD;status("SENDER CONNECTED");detail("");}
            break;}
        case S_HEAD:receive_head();break;
        case S_BODY:receive_body();break;
        default:break;
        }
    }
}
