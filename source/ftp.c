/* Zlib licensed, like DSi Link. FTP protocol implementation; no ftpd code used. */
#include "ftp.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <time.h>
#include <limits.h>
#ifdef ARM9
#include <nds.h>
#endif

#define PATH_CAP 512
#define IO_CAP 32768
#define IDLE_SECONDS 120
#define MAX_SESSIONS 4
enum Transfer { NONE, DOWNLOAD, UPLOAD, LISTING, NAMES, MACHINE };
static FtpLogger logger;
static unsigned log_session;
void ftp_set_logger(FtpLogger callback) { logger=callback; }
static void log_event(const char *fmt, ...) {
    if(!logger) return;
    char text[640]; va_list ap;
    size_t offset=log_session?(size_t)snprintf(text,sizeof(text),"S%u ",log_session):0;
    va_start(ap,fmt); vsnprintf(text+offset,sizeof(text)-offset,fmt,ap); va_end(ap);
    logger(text);
}
typedef struct {
    int control, passive, data;
    uint32_t address, peer;
    bool user, login, quit, eof, discard;
    char root[PATH_CAP], self[PATH_CAP], cwd[PATH_CAP];
    char input[1024], output[4096];
    size_t input_len, output_len, output_sent, used, sent;
    enum Transfer transfer;
    FILE *file;
    DIR *dir;
    char target[PATH_CAP], temporary[PATH_CAP], backup[PATH_CAP];
    char listing_path[PATH_CAP], rename_from[PATH_CAP];
    long restart;
    time_t activity;
    char status[64];
    char pending_command[5], pending_argument[PATH_CAP];
} FtpSession;
static FtpSession sessions[MAX_SESSIONS]={
    {.control=-1,.passive=-1,.data=-1}, {.control=-1,.passive=-1,.data=-1},
    {.control=-1,.passive=-1,.data=-1}, {.control=-1,.passive=-1,.data=-1}
};
static FtpSession *current=&sessions[0];
#define f (*current)
static int listener=-1;
static unsigned char io_buffer[IO_CAP];
static bool network_recovery_requested;
static void control_error(const char *operation,int error){
    const char *kind=error==ECONNABORTED?"aborted":error==ECONNRESET?"peer reset":
        error==ETIMEDOUT?"timed out":error==ENETDOWN?"network down":"socket error";
    log_event("Control %s: %s (%d)",operation,kind,error);
    /* A peer reset/timeout alone does not justify disturbing the radio.
     * Hardware's aborted-control failure accompanied loss of both TCP ports. */
    if(error==ECONNABORTED||error==ENETDOWN||error==ENETUNREACH||error==ENETRESET){
        network_recovery_requested=true;
        log_event("Network recovery requested");
    }
}
bool ftp_network_recovery_ready(void){
    if(!network_recovery_requested)return false;
    for(unsigned i=0;i<MAX_SESSIONS;i++)
        if(sessions[i].transfer!=NONE||sessions[i].output_sent<sessions[i].output_len)return false;
    return true;
}
static bool card_busy(void) {
    for(unsigned i=0;i<MAX_SESSIONS;i++)if(&sessions[i]!=current&&sessions[i].transfer!=NONE)return true;
    return false;
}

static void shut(int *fd) { if (*fd>=0) close(*fd); *fd=-1; }
static bool again(void) { return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR; }
static bool nonblock(int fd) { int on=1; return ioctl(fd,FIONBIO,&on)==0; }
static void keepalive(int fd) {
    int on=1,idle=30,interval=5,probes=3;
    if(setsockopt(fd,SOL_SOCKET,SO_KEEPALIVE,&on,sizeof(on))<0 ||
       setsockopt(fd,IPPROTO_TCP,TCP_KEEPIDLE,&idle,sizeof(idle))<0 ||
       setsockopt(fd,IPPROTO_TCP,TCP_KEEPINTVL,&interval,sizeof(interval))<0 ||
       setsockopt(fd,IPPROTO_TCP,TCP_KEEPCNT,&probes,sizeof(probes))<0)
        log_event("TCP keepalive unavailable (%d)",errno);
}
static void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    int n=vsnprintf(f.output+f.output_len,sizeof(f.output)-f.output_len,fmt,ap);
    va_end(ap);
    if((fmt[0]=='4'||fmt[0]=='5') && strcmp(fmt,"501 No optional settings supported.\r\n")) {
        char text[192]; va_start(ap,fmt); vsnprintf(text,sizeof(text),fmt,ap); va_end(ap);
        text[strcspn(text,"\r\n")]=0; log_event("%s",text);
    }
    if(n<0 || (size_t)n>=sizeof(f.output)-f.output_len) { f.quit=true; return; }
    f.output_len+=(size_t)n;
}
static void end_transfer(void) {
    if(f.pending_command[0])log_event("Queued transfer canceled");
    f.pending_command[0]=0;
    if(f.transfer!=NONE) log_event("Transfer stopped");
    shut(&f.data); shut(&f.passive);
    if(f.file) fclose(f.file);
    if(f.dir) closedir(f.dir);
    f.file=NULL; f.dir=NULL;
    if(f.temporary[0]) remove(f.temporary);
    f.temporary[0]=0; f.transfer=NONE; f.used=f.sent=0; f.eof=false;
    if(f.control>=0) snprintf(f.status,sizeof(f.status),"FTP client connected");
}
static void disconnect(void) {
    if(f.control>=0) log_event("Client disconnected");
    end_transfer(); shut(&f.control);
    f.input_len=f.output_len=f.output_sent=0;
    f.login=f.user=f.quit=f.discard=false; f.cwd[0]='/'; f.cwd[1]=0;
    f.rename_from[0]=0; f.restart=0;
    snprintf(f.status,sizeof(f.status),"FTP waiting on port %u",FTP_PORT);
}
void ftp_stop(void) {
    for(unsigned i=0;i<MAX_SESSIONS;i++){current=&sessions[i];log_session=i+1;disconnect();}
    current=&sessions[0];log_session=0;shut(&listener);network_recovery_requested=false;
}
bool ftp_busy(void) {
    for(unsigned i=0;i<MAX_SESSIONS;i++)if(sessions[i].control>=0)return true;
    return false;
}
const char *ftp_status(void) {
    static char text[64];unsigned clients=0;
    for(unsigned i=0;i<MAX_SESSIONS;i++){
        if(sessions[i].transfer!=NONE)return sessions[i].status;
        if(sessions[i].control>=0)clients++;
    }
    snprintf(text,sizeof(text),"FTP: %u/%u clients",clients,MAX_SESSIONS);return text;
}

/* Normalize components before adding the device prefix. A client cannot select
 * another libfat device or walk above the exported root. */
static bool path(const char *arg, char *virtual_path, char *physical) {
    char combined[PATH_CAP*2];
    int n=snprintf(combined,sizeof(combined),"%s%s%s",arg[0]=='/'?"":f.cwd,
                   arg[0]=='/'?"":"/",arg);
    if(n<0 || (size_t)n>=sizeof(combined)) return false;
    size_t len=1; virtual_path[0]='/'; virtual_path[1]=0;
    char *save=NULL;
    for(char *p=strtok_r(combined,"/",&save);p;p=strtok_r(NULL,"/",&save)) {
        if(!strcmp(p,".")) continue;
        if(!strcmp(p,"..")) {
            while(len>1 && virtual_path[len-1]!='/') len--;
            if(len>1) len--;
            virtual_path[len]=0; continue;
        }
        size_t k=strlen(p);
        if(!k || p[k-1]==' ' || p[k-1]=='.') return false;
        for(size_t i=0;i<k;i++)
            if((unsigned char)p[i]<32 || (unsigned char)p[i]>126 || strchr("\\:*?\"<>|",p[i])) return false;
        if(len+k+2>=PATH_CAP) return false;
        if(len>1) virtual_path[len++]='/';
        memcpy(virtual_path+len,p,k+1); len+=k;
    }
    n=snprintf(physical,PATH_CAP,"%s%s",f.root,virtual_path);
    return n>0 && n<PATH_CAP;
}
static bool reserved(const char *p) {
    size_t n=strlen(p);
    return (n>=15 && !strcasecmp(p+n-15,".dsilink-upload")) ||
           (n>=15 && !strcasecmp(p+n-15,".dsilink-backup"));
}
static bool writable(const char *p) {
    size_t n=strlen(p);
    /* Ancestors are protected too: renaming a containing directory would move
     * the running app and invalidate its Pico Loader return path. */
    return n>strlen(f.root)+1 && !reserved(p) &&
        !(f.self[0] && !strncasecmp(f.self,p,n) && (f.self[n]==0 || f.self[n]=='/'));
}
static bool stat_path(const char *p, struct stat *st) {
    return stat(p,st)==0;
}
static void passive(bool extended) {
    end_transfer();
    int fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_ANY);
    socklen_t len=sizeof(a);
    if(fd<0 || bind(fd,(struct sockaddr *)&a,sizeof(a))<0 || listen(fd,1)<0 ||
       !nonblock(fd) || getsockname(fd,(struct sockaddr *)&a,&len)<0) {
        if(fd>=0) close(fd); say("425 Cannot open passive port.\r\n"); return;
    }
    f.passive=fd;
    unsigned port=ntohs(a.sin_port);
    if(extended) say("229 Entering Extended Passive Mode (|||%u|).\r\n",port);
    else {
        uint32_t ip=ntohl(f.address);
        say("227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\r\n",
            (unsigned)(ip>>24),(unsigned)((ip>>16)&255),(unsigned)((ip>>8)&255),
            (unsigned)(ip&255),port>>8,port&255);
    }
}
static void transfer_command(const char *cmd,const char *arg) {
    char v[PATH_CAP],p[PATH_CAP]; struct stat st;
    if(card_busy()){
        if(strlen(arg)>=sizeof(f.pending_argument)){say("550 Path too long.\r\n");return;}
        snprintf(f.pending_command,sizeof(f.pending_command),"%s",cmd);
        strcpy(f.pending_argument,arg);log_event("Queued %s %s",cmd,arg);return;
    }
    long offset=f.restart; f.restart=0;
    if(f.passive<0 && f.data<0) { say("425 Use PASV or EPSV first.\r\n"); return; }
    if(!path(arg,v,p) || reserved(p)) { say("550 Invalid path.\r\n"); return; }
    if(!strcmp(cmd,"RETR")) {
        if(!stat_path(p,&st) || !S_ISREG(st.st_mode) || offset>st.st_size ||
           !(f.file=fopen(p,"rb"))) { say("550 Cannot read file.\r\n"); return; }
        if(fseek(f.file,offset,SEEK_SET)) { end_transfer(); say("550 Cannot seek.\r\n"); return; }
        f.transfer=DOWNLOAD;
    } else if(!strcmp(cmd,"STOR")) {
        if(offset) { say("504 Upload resume is not supported.\r\n"); return; }
        if(!writable(p) || strlen(p)+16>=PATH_CAP || (stat_path(p,&st) && !S_ISREG(st.st_mode))) {
            say("550 Cannot write this path.\r\n"); return;
        }
        snprintf(f.target,sizeof(f.target),"%s",p);
        snprintf(f.temporary,sizeof(f.temporary),"%s.dsilink-upload",p);
        snprintf(f.backup,sizeof(f.backup),"%s.dsilink-backup",p);
        if(stat_path(f.temporary,&st) || stat_path(f.backup,&st)) {
            f.temporary[0]=0; say("550 Recovery file exists; inspect it locally.\r\n"); return;
        }
        f.file=fopen(f.temporary,"wb");
        if(!f.file) { f.temporary[0]=0; say("550 Cannot create file.\r\n"); return; }
        f.transfer=UPLOAD;
    } else {
        if(offset) { say("504 REST is only supported for RETR.\r\n"); return; }
        f.dir=opendir(p);
        if(!f.dir) { say("550 Cannot open directory.\r\n"); return; }
        snprintf(f.listing_path,sizeof(f.listing_path),"%s",p);
        f.transfer=!strcmp(cmd,"NLST")?NAMES:!strcmp(cmd,"MLSD")?MACHINE:LISTING;
    }
    f.activity=time(NULL); f.used=f.sent=0; f.eof=false;
    log_event("%s %s",cmd,v);
    snprintf(f.status,sizeof(f.status),"FTP %s %.40s",cmd,v);
    say("150 Opening binary data connection.\r\n");
}
static void command(char *line) {
    char *arg=strchr(line,' ');
    if(arg) *arg++=0; else arg=line+strlen(line);
    for(char *c=line;*c;c++) if(*c>='a' && *c<='z') *c-=32;
    if(!strcmp(line,"QUIT")) { end_transfer(); say("221 Goodbye.\r\n"); f.quit=true; return; }
    if(!strcmp(line,"NOOP")) { say("200 OK.\r\n"); return; }
    if(!strcmp(line,"SYST")) { say("215 UNIX Type: L8\r\n"); return; }
    if(!strcmp(line,"AUTH")||!strcmp(line,"PBSZ")||!strcmp(line,"PROT")) { say("502 FTP over TLS is not supported.\r\n"); return; }
    if(!strcmp(line,"FEAT")) { say("211-Features\r\n EPSV\r\n PASV\r\n SIZE\r\n MDTM\r\n REST STREAM\r\n MLST type*;size*;\r\n211 End\r\n"); return; }
    if(!strcmp(line,"USER")) { end_transfer(); f.user=true; f.login=false; say("331 Anonymous access; send password.\r\n"); return; }
    if(!strcmp(line,"PASS")) { f.login=f.user; say(f.login?"230 Logged in.\r\n":"503 Send USER first.\r\n"); return; }
    if(!f.login) { say("530 Log in first.\r\n"); return; }
    if(!strcmp(line,"ABOR")) { log_event("Abort requested"); end_transfer(); say("226 Aborted.\r\n"); return; }
    if(f.transfer!=NONE||f.pending_command[0]) { say("450 Transfer in progress.\r\n"); return; }
    if(card_busy()&&(!strcmp(line,"RNFR")||!strcmp(line,"RNTO")||!strcmp(line,"MKD")||!strcmp(line,"RMD")||!strcmp(line,"DELE"))){
        say("450 Card transfer in progress; retry file operation.\r\n");return;
    }
    if(!strcmp(line,"PASV") || !strcmp(line,"EPSV")) { passive(!strcmp(line,"EPSV")); return; }
    if(!strcmp(line,"TYPE")) { say(!strcmp(arg,"I")||!strcmp(arg,"A")?"200 Type accepted; bytes preserved.\r\n":"504 Unsupported type.\r\n"); return; }
    if(!strcmp(line,"MODE") || !strcmp(line,"STRU")) { say(!strcmp(arg,!strcmp(line,"MODE")?"S":"F")?"200 OK.\r\n":"504 Unsupported mode.\r\n"); return; }
    if(!strcmp(line,"OPTS")) { say("501 No optional settings supported.\r\n"); return; }
    if(!strcmp(line,"PWD") || !strcmp(line,"XPWD")) { say("257 \"%s\"\r\n",f.cwd); return; }
    if(!strcmp(line,"REST")) {
        char *end; errno=0; long n=strtol(arg,&end,10);
        if(!*arg || *end || n<0 || errno || n>INT_MAX) say("501 Invalid offset.\r\n");
        else { f.restart=n; say("350 Restart position accepted for RETR.\r\n"); } return;
    }
    if(!strcmp(line,"RETR") || !strcmp(line,"STOR") || !strcmp(line,"LIST") || !strcmp(line,"NLST") || !strcmp(line,"MLSD")) {
        transfer_command(line,arg); return;
    }
    char v[PATH_CAP],p[PATH_CAP]; struct stat st;
    if(!strcmp(line,"CDUP")) arg="..";
    if(!path(arg,v,p) || reserved(p)) { say("550 Invalid path.\r\n"); return; }
    if(!strcmp(line,"CWD") || !strcmp(line,"CDUP")) {
        if(stat_path(p,&st) && S_ISDIR(st.st_mode)) { strcpy(f.cwd,v); log_event("Directory %s",v); say("250 Directory changed.\r\n"); }
        else say("550 Directory unavailable.\r\n");
    } else if(!strcmp(line,"MLST")) {
        if(!stat_path(p,&st)) say("550 Path unavailable.\r\n");
        else say("250-Listing\r\n type=%s;size=%lu; %s\r\n250 End\r\n",S_ISDIR(st.st_mode)?"dir":"file",(unsigned long)st.st_size,v);
    } else if(!strcmp(line,"SIZE") || !strcmp(line,"MDTM")) {
        if(!stat_path(p,&st) || !S_ISREG(st.st_mode)) say("550 File unavailable.\r\n");
        else if(!strcmp(line,"SIZE")) say("213 %lu\r\n",(unsigned long)st.st_size);
        else { char date[32]; struct tm *t=gmtime(&st.st_mtime); if(t && strftime(date,sizeof(date),"%Y%m%d%H%M%S",t)) say("213 %s\r\n",date); else say("550 Timestamp unavailable.\r\n"); }
    } else if(!strcmp(line,"RNFR")) {
        f.rename_from[0]=0;
        if(writable(p) && stat_path(p,&st)) { strcpy(f.rename_from,p); say("350 Send RNTO.\r\n"); }
        else say("550 Cannot rename this path.\r\n");
    } else if(!strcmp(line,"RNTO")) {
        bool ok=f.rename_from[0] && writable(p) && !stat_path(p,&st) && rename(f.rename_from,p)==0;
        if(ok) { log_event("Renamed %s",f.rename_from+strlen(f.root)); log_event("To %s",v); }
        f.rename_from[0]=0; say(ok?"250 Renamed.\r\n":"550 Rename failed; destination must not exist.\r\n");
    } else if(!strcmp(line,"MKD") || !strcmp(line,"RMD") || !strcmp(line,"DELE")) {
        bool ok=false;
        if(writable(p)) {
            if(!strcmp(line,"MKD")) ok=mkdir(p,0777)==0;
            else if(!strcmp(line,"RMD")) ok=rmdir(p)==0;
            else if(stat_path(p,&st) && S_ISREG(st.st_mode)) ok=remove(p)==0;
        }
        if(ok) log_event("%s %s",line,v);
        say(ok?"250 Done.\r\n":"550 File operation failed.\r\n");
    } else say("502 Command not implemented.\r\n");
}
static void complete_upload(void) {
    bool ok=fflush(f.file)==0;
    if(ok) ok=fsync(fileno(f.file))==0;
    if(fclose(f.file)) ok=false; f.file=NULL;
    struct stat st; bool old=stat_path(f.target,&st);
    if(ok && old) ok=rename(f.target,f.backup)==0;
    if(ok && rename(f.temporary,f.target)) {
        if(old) rename(f.backup,f.target);
        ok=false;
    }
    if(ok) { f.temporary[0]=0; if(old) remove(f.backup); }
    if(ok) { log_event("Upload saved %s",f.target+strlen(f.root)); f.transfer=NONE; }
    end_transfer(); say(ok?"226 Upload saved.\r\n":"451 Card write or replacement failed.\r\n");
}
static bool listing(void) {
    /* Scan at most eight entries per poll, even when most entries are filtered. */
    for(unsigned i=0;i<8;i++) {
        struct dirent *e=readdir(f.dir);
        if(!e) { f.eof=true; return true; }
        if(!strcmp(e->d_name,".") || !strcmp(e->d_name,"..") || reserved(e->d_name)) continue;
        bool printable=true;
        for(const unsigned char *c=(unsigned char *)e->d_name;*c;c++) if(*c<32 || *c>126) printable=false;
        if(!printable) continue;
        char p[PATH_CAP]; struct stat st;
        int n=snprintf(p,sizeof(p),"%s/%s",f.listing_path,e->d_name);
        if(n<0 || (size_t)n>=sizeof(p) || !stat_path(p,&st)) continue;
        bool dir=S_ISDIR(st.st_mode);
        if(f.transfer==NAMES) n=snprintf((char *)io_buffer,IO_CAP,"%s\r\n",e->d_name);
        else if(f.transfer==MACHINE) n=snprintf((char *)io_buffer,IO_CAP,"type=%s;size=%lu; %s\r\n",dir?"dir":"file",(unsigned long)st.st_size,e->d_name);
        else {
            char date[32]; struct tm *t=gmtime(&st.st_mtime);
            if(!t || !strftime(date,sizeof(date),"%b %d  %Y",t)) strcpy(date,"Jan 01  1970");
            n=snprintf((char *)io_buffer,IO_CAP,"%crwxrwxrwx 1 ftp ftp %lu %s %s\r\n",dir?'d':'-',(unsigned long)st.st_size,date,e->d_name);
        }
        if(n>0 && n<IO_CAP) { f.used=(size_t)n; f.sent=0; return true; }
    }
    return false;
}
static void data_poll(void) {
    if(f.passive>=0 && f.data<0) {
        struct sockaddr_in a; socklen_t len=sizeof(a);
        int fd=accept(f.passive,(struct sockaddr *)&a,&len);
        if(fd>=0) {
            if(a.sin_addr.s_addr!=f.peer || !nonblock(fd)) { log_event("Rejected data connection");close(fd); }
            else { f.data=fd; shut(&f.passive); f.activity=time(NULL); }
        }
    }
    if(f.data<0 || f.transfer==NONE) return;
    /* Yield within bursts so small TCP reads do not force a VBlank wait after
     * every few packets. Card access remains owned by this service. */
    for(unsigned i=0;i<16;i++) {
#ifdef ARM9
        if(i && i%4==0) cothread_yield();
#endif
        if(f.transfer==UPLOAD) {
            int n=recv(f.data,io_buffer,IO_CAP,0);
            if(n<0) { if(again()) return; end_transfer(); say("426 Data connection failed.\r\n"); return; }
            if(!n) { complete_upload(); return; }
            f.activity=time(NULL);
            if(fwrite(io_buffer,1,(size_t)n,f.file)!=(size_t)n) { end_transfer(); say("451 Card write failed.\r\n"); return; }
        } else {
            if(f.sent==f.used && !f.eof) {
                f.used=f.sent=0;
                if(f.transfer==DOWNLOAD) {
                    f.used=fread(io_buffer,1,IO_CAP,f.file);
                    if(ferror(f.file)) { end_transfer(); say("451 Card read failed.\r\n"); return; }
                    f.eof=feof(f.file)!=0;
                } else if(!listing()) return;
            }
            if(f.sent<f.used) {
                int n=send(f.data,io_buffer+f.sent,f.used-f.sent,0);
                if(n<0 && again()) return;
                if(n<=0) { end_transfer(); say("426 Data connection failed.\r\n"); return; }
                f.sent+=(size_t)n; f.activity=time(NULL);
            }
            if(f.sent==f.used && f.eof) { log_event(f.transfer==DOWNLOAD?"Download complete":"Listing complete"); f.transfer=NONE; end_transfer(); say("226 Transfer complete.\r\n"); return; }
        }
    }
}
bool ftp_start(const char *root,const char *self,uint32_t address) {
    ftp_stop();
    if(strlen(root)>=sizeof(f.root) || strlen(self)>=sizeof(f.self)) return false;
    strcpy(f.root,root); strcpy(f.self,self); f.address=address;
    if(self[0]) {
        char v[PATH_CAP],p[PATH_CAP]; size_t n=strlen(root);
        if(strncasecmp(self,root,n) || !path(self+n,v,p)) return false;
        strcpy(f.self,p);
    }
    struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_port=htons(FTP_PORT); a.sin_addr.s_addr=htonl(INADDR_ANY);
    for(unsigned i=1;i<MAX_SESSIONS;i++){
        strcpy(sessions[i].root,f.root);strcpy(sessions[i].self,f.self);sessions[i].address=address;
    }
    listener=socket(AF_INET,SOCK_STREAM,0);
    if(listener>=0) { int reuse=1; setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse)); }
    if(listener<0 || bind(listener,(struct sockaddr *)&a,sizeof(a))<0 || listen(listener,MAX_SESSIONS)<0 || !nonblock(listener)) { ftp_stop(); return false; }
    log_event("Listening on port %u",FTP_PORT);
    return true;
}
static void poll_session(void) {
    if(f.control<0)return;
    if(time(NULL)-f.activity>IDLE_SECONDS) { log_event("Idle timeout after %d seconds",IDLE_SECONDS); disconnect(); return; }
    if(f.output_sent<f.output_len) {
        int n=send(f.control,f.output+f.output_sent,f.output_len-f.output_sent,0);
        if(n<0 && again())return;
        if(n<=0) { control_error("send",errno); disconnect(); return; }
        f.output_sent+=(size_t)n;
        if(f.output_sent<f.output_len) return;
        f.output_len=f.output_sent=0;
    }
    if(f.quit) { log_event("Client quit");disconnect(); return; }
    /* One command per poll preserves reply ordering under pipelined input. */
    for(unsigned i=0;i<256;i++) {
        char c; int n=recv(f.control,&c,1,0);
        if(n<0) { if(!again()) { control_error("receive",errno); disconnect(); } break; }
        if(!n) { log_event("Control peer closed");disconnect(); return; }
        f.activity=time(NULL);
        if(c=='\n') {
            if(f.discard) { f.discard=false; f.input_len=0; say("500 Invalid or overlong command.\r\n"); }
            else { if(f.input_len && f.input[f.input_len-1]=='\r') f.input_len--; f.input[f.input_len]=0; command(f.input); f.input_len=0; }
            break;
        }
        if(!c || ((unsigned char)c<32 && c!='\r') || f.input_len+1>=sizeof(f.input)) f.discard=true;
        if(!f.discard) f.input[f.input_len++]=c;
    }
    if(f.control>=0) data_poll();
}
void ftp_poll(void){
    if(listener<0)return;
    struct sockaddr_in a;socklen_t len=sizeof(a);
    int fd=accept(listener,(struct sockaddr *)&a,&len);
    if(fd>=0){
        unsigned slot=0;while(slot<MAX_SESSIONS&&sessions[slot].control>=0)slot++;
        if(slot==MAX_SESSIONS){
            static const char busy[]="421 Four FTP sessions already connected.\r\n";
            if(nonblock(fd))(void)send(fd,busy,sizeof(busy)-1,0);
            close(fd);log_event("Rejected connection: 4 sessions full");
        }else if(!nonblock(fd))close(fd);
        else{
            current=&sessions[slot];log_session=slot+1;f.control=fd;f.peer=a.sin_addr.s_addr;f.activity=time(NULL);keepalive(fd);
            log_event("Client connected %s (slot %u)",inet_ntoa(a.sin_addr),slot+1);
            say("220 DSi Link FTP; anonymous LAN access.\r\n");
        }
    }else if(!again())log_event("Listener accept failed (%d)",errno);
    static unsigned first;
    for(unsigned n=0;n<MAX_SESSIONS;n++){
        current=&sessions[(first+n)%MAX_SESSIONS];log_session=(first+n)%MAX_SESSIONS+1;poll_session();
        if(f.control>=0&&!f.quit&&f.pending_command[0]&&!card_busy()){
            char cmd[5],arg[PATH_CAP];strcpy(cmd,f.pending_command);strcpy(arg,f.pending_argument);
            f.pending_command[0]=0;transfer_command(cmd,arg);
        }
    }
    first=(first+1)%MAX_SESSIONS;current=&sessions[0];log_session=0;
}
