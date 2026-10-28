#include "../source/event_log.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    event_log_write("FTP","CWD /homebrew/abcdefghijklmnopqrstuvwxyz/long-path");
    assert(event_log_count()==2);
    assert(strlen(event_log_line(0))==EVENT_LOG_WIDTH);
    assert(strstr(event_log_line(0),"[FTP] CWD /homebrew/"));
    event_log_write("LINK","bad\033[2J\r\ntext");
    assert(!strchr(event_log_line(2),'\033'));
    assert(!strchr(event_log_line(2),'\n'));
    for(unsigned i=0;i<200;i++){char line[32];snprintf(line,sizeof(line),"Event %u",i);event_log_write("TEST",line);}
    assert(event_log_count()==128);
    assert(!strcmp(event_log_line(0),"[TEST] Event 72"));
    assert(!strcmp(event_log_line(127),"[TEST] Event 199"));
    assert(!strcmp(event_log_line(128),""));
    puts("Event log wrapping, control filtering and bounded history: PASS");
    return 0;
}
