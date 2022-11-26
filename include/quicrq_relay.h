#ifndef QUICRQ_RELAY_H
#define QUICRQ_RELAY_H

#include "quicrq.h"


#ifdef __cplusplus
extern "C" {
#endif


    /* Enable the relay */
    int quicrq_enable_relay(quicrq_ctx_t* qr_ctx, const char * sni, const struct sockaddr * addr, quicrq_transport_mode_enum transport_mode);

    /* Enable origin */
    int quicrq_enable_origin(quicrq_ctx_t* qr_ctx, quicrq_transport_mode_enum transport_mode);

    /* Disable the relay */
    void quicrq_disable_relay(quicrq_ctx_t* qr_ctx);

#ifdef __cplusplus
}
#endif

#endif /* QUICRQ_RELAY_H */
