#ifndef WOLFTRUST_ZEPHYR_CLIENT_H
#define WOLFTRUST_ZEPHYR_CLIENT_H

int wt_zephyr_client_init(const char *tag);
int wt_zephyr_client_ping(void);
const char *wt_zephyr_client_status_string(int rc);

#endif
