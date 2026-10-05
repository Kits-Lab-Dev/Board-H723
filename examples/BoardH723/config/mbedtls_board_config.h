/* Конфиг Mbed TLS 3.6 для Доски-H723 (MBEDTLS_CONFIG_FILE).
 *
 * Пользователи:
 *   - модуль ssl для Lua (libs/tls) — TLS 1.2 клиент для HTTPS;
 *   - NimBLE (ble_sm_alg.c) — AES, AES-CMAC, ECDH P-256 для BLE Secure Connections.
 *
 * Собирается только то, что нужно этим двоим; остальное выключено ради Flash.
 * Полный список опций с описаниями — third_party/mbedtls/include/mbedtls/mbedtls_config.h.
 */
#ifndef MBEDTLS_BOARD_CONFIG_H
#define MBEDTLS_BOARD_CONFIG_H

/* ---- Платформа ---------------------------------------------------------- */
#define MBEDTLS_PLATFORM_C
/* Память — куча FreeRTOS (calloc/free переопределены в CMakeLists.txt).
 * В SDRAM (sdramCalloc) не переносить: на плате с кучей в SDRAM проверка подписей
 * в рукопожатии стабильно не проходит, хотя самотесты Mbed TLS там проходят — причина
 * не найдена. Соединение занимает ~25 КБ кучи, поэтому cafile — только нужные корни. */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_NO_PLATFORM_ENTROPY      /* нет /dev/urandom */
#define MBEDTLS_ENTROPY_HARDWARE_ALT     /* mbedtls_hardware_poll() — аппаратный RNG, libs/tls/tls_port.c */
#define MBEDTLS_HAVE_TIME                /* время — часы RTC (get_time() из libs/ntp) */
#define MBEDTLS_HAVE_TIME_DATE           /* проверка срока действия сертификатов */
#define MBEDTLS_PLATFORM_TIME_ALT
#define MBEDTLS_PLATFORM_MS_TIME_ALT     /* mbedtls_ms_time() — тики FreeRTOS */
#define MBEDTLS_PLATFORM_GMTIME_R_ALT    /* mbedtls_platform_gmtime_r() — newlib gmtime_r */

/* ---- Криптография ------------------------------------------------------- */
#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES           /* таблицы AES во Flash, а не в RAM */
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CMAC_C                   /* NimBLE: AES-CMAC */
#define MBEDTLS_CIPHER_MODE_CBC          /* PEM-ключи с паролем */

#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C                   /* старые корневые сертификаты */
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_MD5_C                    /* PEM-ключи с паролем (DES/AES-CBC + MD5) */

#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/* ---- Ключи и сертификаты ------------------------------------------------ */
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_PARSE_EC_EXTENDED
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PEM_WRITE_C              /* cert:pem() в Lua */
#define MBEDTLS_FS_IO                    /* загрузка сертификатов и ключей из файлов (FatFs) */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* ---- TLS ---------------------------------------------------------------- */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_SSL_SERVER_NAME_INDICATION   /* SNI: без него CDN рвут соединение */
#define MBEDTLS_SSL_ALPN
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE    /* getpeercertificate() в Lua */
#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#define MBEDTLS_SSL_SESSION_TICKETS

/* Запись TLS от сервера может быть до 16 КБ — меньше входной буфер делать нельзя.
 * Исходящий буфер достаточно 4 КБ: длинные данные режутся на записи. */
#define MBEDTLS_SSL_IN_CONTENT_LEN  16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096

/* Ошибки строками — для сообщений в Lua */
#define MBEDTLS_ERROR_C

#endif /* MBEDTLS_BOARD_CONFIG_H */
