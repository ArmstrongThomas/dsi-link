#ifndef DSI_LINK_FTP_H
#define DSI_LINK_FTP_H
#include <stdbool.h>
#include <stdint.h>
#define FTP_PORT 5000
typedef void (*FtpLogger)(const char *message);
void ftp_set_logger(FtpLogger logger);
bool ftp_start(const char *root, const char *self, uint32_t address);
void ftp_poll(void);
void ftp_stop(void);
bool ftp_busy(void);
bool ftp_network_recovery_ready(void);
const char *ftp_status(void);
#endif
