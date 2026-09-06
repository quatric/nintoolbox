#ifndef SZS_SENTRY_H
#define SZS_SENTRY_H 1

#include "dclib-basics.h"

#ifdef __cplusplus
extern "C" {
#endif

void szs_sentry_init(void);
void szs_sentry_shutdown(void);
void szs_sentry_capture_message(ccp message);

#ifdef __cplusplus
}
#endif

#endif // SZS_SENTRY_H
