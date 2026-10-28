#include "../source/ftp.h"
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <string.h>
static volatile sig_atomic_t running=1;
static FILE *events;
#ifdef __linux__
static const char *fault_path;
#undef recv
ssize_t recv(int fd,void *data,size_t size,int flags);
ssize_t ftp_host_recv(int fd,void *data,size_t size,int flags){
    if(size==1){
        FILE *fault=fopen(fault_path,"r");
        if(fault){
            unsigned port=0;int error=0;int parsed=fscanf(fault,"%u %d",&port,&error);fclose(fault);
            struct sockaddr_in peer;socklen_t len=sizeof(peer);
            if(parsed==2&&getpeername(fd,(struct sockaddr *)&peer,&len)==0&&ntohs(peer.sin_port)==port){
                remove(fault_path);errno=error;return -1;
            }
        }
    }
    return recv(fd,data,size,flags);
}
#endif
static void log_event(const char *s){if(events){fprintf(events,"%s\n",s);fflush(events);}}
static void stop(int signal_number) { (void)signal_number; running=0; }
int main(int argc,char **argv) {
    if(argc!=5) return 2;
#ifdef __linux__
    fault_path=argv[4];
#endif
    events=fopen(argv[3],"w");if(!events)return 4;
    ftp_set_logger(log_event);
    signal(SIGTERM,stop); signal(SIGINT,stop); signal(SIGPIPE,SIG_IGN);
    if(!ftp_start(argv[1],argv[2],inet_addr("127.0.0.1"))) return 3;
    puts("READY"); fflush(stdout);
    while(running) {
        ftp_poll();
        if(ftp_network_recovery_ready()){
            log_event("HOST network recovery ready");
            ftp_stop();if(!ftp_start(argv[1],argv[2],inet_addr("127.0.0.1")))return 5;
        }
        usleep(1000);
    }
    ftp_stop(); fclose(events); return 0;
}
