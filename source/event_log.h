#ifndef DSI_LINK_EVENT_LOG_H
#define DSI_LINK_EVENT_LOG_H
#include <stddef.h>
#include <stdint.h>
#define EVENT_LOG_WIDTH 31
#define EVENT_LOG_LINES 128
void event_log_write(const char *tag, const char *message);
size_t event_log_count(void);
const char *event_log_line(size_t index);
uint32_t event_log_revision(void);
#endif
