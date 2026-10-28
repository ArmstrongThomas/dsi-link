#include "../source/ftp.h"
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
static volatile sig_atomic_t running=1;
static void stop(int signal_number) { (void)signal_number; running=0; }
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    signal(SIGTERM,stop); signal(SIGINT,stop); signal(SIGPIPE,SIG_IGN);
    if(!ftp_start(argv[1],argv[2],inet_addr("127.0.0.1"))) return 3;
    puts("READY"); fflush(stdout);
    while(running) { ftp_poll(); usleep(1000); }
    ftp_stop(); return 0;
}
