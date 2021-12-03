#ifndef QUICRQ_RELAY_H
#define QUICRQ_RELAY_H

#include "quicrq.h"


#ifdef __cplusplus
extern "C" {
#endif


    /* Enable the relay */
    int quicrq_enable_relay(quicrq_ctx_t* qr_ctx, const char * sni, const struct sockaddr * addr, int use_datagrams);


#ifdef __cplusplus
}
#endif

#endif /* QUICRQ_RELAY_H */
