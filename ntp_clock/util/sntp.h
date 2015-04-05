#ifndef __SNTP_H__
#define __SNTP_H__



#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

void sntp_init(char **sntp_server_list, unsigned polling_interval);
void sntp_stop(void);
uint64_t sntp_get_time(void);
bool sntp_conn_established(void);


#ifdef __cplusplus
}
#endif

#endif /* __SNTP_H__ */
