#include "event_log.h"
#include <stdio.h>
#include <string.h>

static char lines[EVENT_LOG_LINES][EVENT_LOG_WIDTH+1];
static size_t count, next;
static uint32_t revision;

static void append(const char *text) {
    snprintf(lines[next],sizeof(lines[next]),"%s",text);
    next=(next+1)%EVENT_LOG_LINES;
    if(count<EVENT_LOG_LINES) count++;
    revision++;
}
void event_log_write(const char *tag,const char *message) {
    char line[EVENT_LOG_WIDTH+1];
    size_t used=0;
    line[used++]='[';
    for(size_t i=0;tag[i] && i<8;i++) {
        unsigned char c=(unsigned char)tag[i];
        line[used++]=(c>=32 && c<=126)?(char)c:'?';
    }
    line[used++]=']'; line[used++]=' ';
    for(size_t i=0;message[i];i++) {
        if(used==EVENT_LOG_WIDTH) {
            line[used]=0; append(line); used=2; line[0]=line[1]=' ';
        }
        unsigned char c=(unsigned char)message[i];
        line[used++]=(c>=32 && c<=126)?(char)c:'?';
    }
    line[used]=0; append(line);
}
size_t event_log_count(void) { return count; }
const char *event_log_line(size_t index) {
    if(index>=count) return "";
    return lines[(next+EVENT_LOG_LINES-count+index)%EVENT_LOG_LINES];
}
uint32_t event_log_revision(void) { return revision; }
