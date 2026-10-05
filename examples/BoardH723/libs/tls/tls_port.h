#ifndef TLS_PORT_H
#define TLS_PORT_H

#include <stddef.h>

/* Генератор случайных чисел для Mbed TLS (f_rng): CTR_DRBG, засеянный
 * аппаратным RNG. Потокобезопасен; p_rng не используется. */
int tls_random(void *p_rng, unsigned char *output, size_t len);

/* Аппаратный RNG: len случайных байт, 0 — успех. Потокобезопасен.
 * Им же засевается wolfCrypt (CUSTOM_RAND_GENERATE_SEED). */
int hw_random(unsigned char *output, unsigned int len);

#endif /* TLS_PORT_H */
