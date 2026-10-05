/* Модуль ssl для Lua на Mbed TLS.
 *
 * Повторяет C-интерфейс LuaSec 1.3.2 (модули ssl.core, ssl.context, ssl.x509,
 * ssl.config), поэтому Lua-часть LuaSec (lib/ssl.lua, lib/ssl/https.lua на SD)
 * работает без изменений. Устроено по образцу LuaSec (MIT, (C) 2006-2023
 * Bruno Silvestre и др.), но на Mbed TLS вместо OpenSSL.
 *
 * Что поддерживается:
 *   - только режим клиента, TLS 1.2;
 *   - проверка сертификата сервера: verify = "peer" + cafile (PEM-файл с корневыми
 *     сертификатами); без cafile — verify = "none";
 *   - клиентский сертификат и ключ (certificate, key, password), SNI, ALPN.
 * Остальные параметры LuaSec (ciphers, options, curve, depth, dhparam)
 * принимаются и игнорируются — набор алгоритмов задан конфигом Mbed TLS.
 */
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include <luasocket/buffer.h>
#include <luasocket/io.h>
#include <luasocket/socket.h>
#include <luasocket/timeout.h>

#include "FreeRTOS.h"
#include "FreeRTOS_Sockets.h"

#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/pem.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include "tls_port.h"

#define LSSL_CONTEXT "SSL:Context"
#define LSSL_CONNECTION "SSL:Connection"
#define LSSL_CERT "SSL:Certificate"

#define LSSL_STATE_NEW 1
#define LSSL_STATE_CONNECTED 2
#define LSSL_STATE_CLOSED 3

/* Код ошибки TLS для io (как LSEC_IO_SSL в LuaSec): текст берётся из ssl->error */
#define LSSL_IO_SSL -100

#define LSSL_ALPN_MAX 4

/* ---------------------------------------------------------------------- */
/* Контекст                                                               */
/* ---------------------------------------------------------------------- */

typedef struct
{
    mbedtls_ssl_config conf;
    mbedtls_x509_crt ca;
    mbedtls_x509_crt cert;
    mbedtls_pk_context key;
    int hasCa;
    int hasCert;
    int hasKey;
    int verify; /* MBEDTLS_SSL_VERIFY_* */
    const char *alpn[LSSL_ALPN_MAX + 1];
    char alpnBuf[64];
} t_context;

static t_context *checkcontext(lua_State *L, int idx)
{
    return (t_context *)luaL_checkudata(L, idx, LSSL_CONTEXT);
}

static int pusherror(lua_State *L, const char *what, int err)
{
    char buf[96];
    mbedtls_strerror(err, buf, sizeof(buf));
    lua_pushnil(L);
    lua_pushfstring(L, "%s: %s (-0x%04x)", what, buf, (unsigned)-err);
    return 2;
}

static int ctx_create(lua_State *L)
{
    const char *proto = luaL_optstring(L, 1, "any");
    if (strcmp(proto, "any") && strcmp(proto, "tlsv1_2"))
    {
        lua_pushnil(L);
        lua_pushfstring(L, "invalid protocol (%s)", proto);
        return 2;
    }

    t_context *ctx = (t_context *)lua_newuserdatauv(L, sizeof(t_context), 0);
    memset(ctx, 0, sizeof(*ctx));
    mbedtls_ssl_config_init(&ctx->conf);
    mbedtls_x509_crt_init(&ctx->ca);
    mbedtls_x509_crt_init(&ctx->cert);
    mbedtls_pk_init(&ctx->key);
    luaL_setmetatable(L, LSSL_CONTEXT);

    int ret = mbedtls_ssl_config_defaults(&ctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
        return pusherror(L, "error creating context", ret);
    mbedtls_ssl_conf_rng(&ctx->conf, tls_random, NULL);
    /* Как в LuaSec: по умолчанию сертификат сервера не проверяется */
    ctx->verify = MBEDTLS_SSL_VERIFY_NONE;
    mbedtls_ssl_conf_authmode(&ctx->conf, ctx->verify);
    return 1;
}

static int ctx_destroy(lua_State *L)
{
    t_context *ctx = checkcontext(L, 1);
    if (ctx->verify >= 0)
    {
        mbedtls_ssl_config_free(&ctx->conf);
        mbedtls_x509_crt_free(&ctx->ca);
        mbedtls_x509_crt_free(&ctx->cert);
        mbedtls_pk_free(&ctx->key);
        ctx->verify = -1;
    }
    return 0;
}

static int ctx_setmode(lua_State *L)
{
    checkcontext(L, 1);
    const char *mode = luaL_checkstring(L, 2);
    if (strcmp(mode, "client") == 0)
    {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    lua_pushfstring(L, "mode not supported (%s): only client", mode);
    return 2;
}

static int ctx_locations(lua_State *L)
{
    t_context *ctx = checkcontext(L, 1);
    const char *cafile = luaL_optstring(L, 2, NULL);
    const char *capath = luaL_optstring(L, 3, NULL);
    int ret = 0;
    if (cafile)
        ret = mbedtls_x509_crt_parse_file(&ctx->ca, cafile);
    if (ret >= 0 && capath)
        ret = mbedtls_x509_crt_parse_path(&ctx->ca, capath);
    if (ret < 0)
    {
        lua_pushboolean(L, 0);
        char buf[96];
        mbedtls_strerror(ret, buf, sizeof(buf));
        lua_pushfstring(L, "error loading CA locations (%s)", buf);
        return 2;
    }
    /* ret > 0 — часть сертификатов не разобрана; остальные загружены */
    ctx->hasCa = 1;
    mbedtls_ssl_conf_ca_chain(&ctx->conf, &ctx->ca, NULL);
    lua_pushboolean(L, 1);
    return 1;
}

static int ctx_loadcert(lua_State *L)
{
    t_context *ctx = checkcontext(L, 1);
    const char *file = luaL_checkstring(L, 2);
    int ret = mbedtls_x509_crt_parse_file(&ctx->cert, file);
    if (ret != 0)
    {
        lua_pushboolean(L, 0);
        char buf[96];
        mbedtls_strerror(ret, buf, sizeof(buf));
        lua_pushfstring(L, "error loading certificate (%s)", buf);
        return 2;
    }
    ctx->hasCert = 1;
    if (ctx->hasKey)
        mbedtls_ssl_conf_own_cert(&ctx->conf, &ctx->cert, &ctx->key);
    lua_pushboolean(L, 1);
    return 1;
}

static int ctx_loadkey(lua_State *L)
{
    t_context *ctx = checkcontext(L, 1);
    const char *file = luaL_checkstring(L, 2);
    const char *pwd = NULL;
    if (lua_type(L, 3) == LUA_TSTRING)
        pwd = lua_tostring(L, 3);
    else if (lua_type(L, 3) == LUA_TFUNCTION)
    {
        lua_pushvalue(L, 3);
        lua_call(L, 0, 1);
        pwd = lua_tostring(L, -1);
    }
    int ret = mbedtls_pk_parse_keyfile(&ctx->key, file, pwd, tls_random, NULL);
    if (ret != 0)
    {
        lua_pushboolean(L, 0);
        char buf[96];
        mbedtls_strerror(ret, buf, sizeof(buf));
        lua_pushfstring(L, "error loading private key (%s)", buf);
        return 2;
    }
    ctx->hasKey = 1;
    if (ctx->hasCert)
        mbedtls_ssl_conf_own_cert(&ctx->conf, &ctx->cert, &ctx->key);
    lua_pushboolean(L, 1);
    return 1;
}

static int ctx_checkkey(lua_State *L)
{
    t_context *ctx = checkcontext(L, 1);
    int ok = ctx->hasCert && ctx->hasKey &&
             mbedtls_pk_check_pair(&ctx->cert.pk, &ctx->key, tls_random, NULL) == 0;
    lua_pushboolean(L, ok);
    return 1;
}

static int ctx_setverify(lua_State *L)
{
    t_context *ctx = checkcontext(L, 1);
    int verify = MBEDTLS_SSL_VERIFY_NONE;
    int top = lua_gettop(L);
    for (int i = 2; i <= top; i++)
    {
        const char *s = luaL_checkstring(L, i);
        if (strcmp(s, "none") == 0)
            continue;
        if (strcmp(s, "peer") == 0 || strcmp(s, "fail_if_no_peer_cert") == 0 ||
            strcmp(s, "client_once") == 0)
            verify = MBEDTLS_SSL_VERIFY_REQUIRED;
        else
        {
            lua_pushboolean(L, 0);
            lua_pushfstring(L, "invalid verify option (%s)", s);
            return 2;
        }
    }
    ctx->verify = verify;
    mbedtls_ssl_conf_authmode(&ctx->conf, verify);
    lua_pushboolean(L, 1);
    return 1;
}

/* setalpn(ctx, wire) — список протоколов в формате ALPN: len|name|len|name... */
static int ctx_setalpn(lua_State *L)
{
    t_context *ctx = checkcontext(L, 1);
    size_t len;
    const char *wire = luaL_checklstring(L, 2, &len);
    if (len >= sizeof(ctx->alpnBuf))
    {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "ALPN list too long");
        return 2;
    }
    int n = 0;
    size_t i = 0, o = 0;
    while (i < len && n < LSSL_ALPN_MAX)
    {
        size_t l = (unsigned char)wire[i++];
        if (i + l > len)
            break;
        ctx->alpn[n++] = &ctx->alpnBuf[o];
        memcpy(&ctx->alpnBuf[o], &wire[i], l);
        o += l;
        ctx->alpnBuf[o++] = 0;
        i += l;
    }
    ctx->alpn[n] = NULL;
    int ret = mbedtls_ssl_conf_alpn_protocols(&ctx->conf, ctx->alpn);
    lua_pushboolean(L, ret == 0);
    return 1;
}

/* Параметры, которые на Mbed TLS задаются конфигом сборки */
static int ctx_accept(lua_State *L)
{
    checkcontext(L, 1);
    lua_pushboolean(L, 1);
    return 1;
}

static int ctx_tostring(lua_State *L)
{
    lua_pushfstring(L, "SSL context: %p", checkcontext(L, 1));
    return 1;
}

static int ctx_setverifyext(lua_State *L)
{
    return ctx_accept(L);
}

static const luaL_Reg ctx_funcs[] = {
    {"create", ctx_create},
    {"locations", ctx_locations},
    {"loadcert", ctx_loadcert},
    {"loadkey", ctx_loadkey},
    {"checkkey", ctx_checkkey},
    {"setalpn", ctx_setalpn},
    {"setcipher", ctx_accept},
    {"setciphersuites", ctx_accept},
    {"setdepth", ctx_accept},
    {"setdhparam", ctx_accept},
    {"setverify", ctx_setverify},
    {"setoptions", ctx_accept},
    {"setmode", ctx_setmode},
    {"setcurve", ctx_accept},
    {"setcurveslist", ctx_accept},
    {NULL, NULL}};

static const luaL_Reg ctx_meta[] = {
    {"__close", ctx_destroy},
    {"__gc", ctx_destroy},
    {"__tostring", ctx_tostring},
    {NULL, NULL}};

static const luaL_Reg ctx_methods[] = {
    {"setverifyext", ctx_setverifyext},
    {NULL, NULL}};

int luaopen_ssl_context(lua_State *L)
{
    luaL_newmetatable(L, LSSL_CONTEXT);
    luaL_setfuncs(L, ctx_meta, 0);
    luaL_newlib(L, ctx_methods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    luaL_newlib(L, ctx_funcs);
    return 1;
}

/* ---------------------------------------------------------------------- */
/* Сертификат (ssl.x509)                                                  */
/* ---------------------------------------------------------------------- */

typedef struct
{
    mbedtls_x509_crt crt;
} t_cert;

static t_cert *checkcert(lua_State *L, int idx)
{
    return (t_cert *)luaL_checkudata(L, idx, LSSL_CERT);
}

/* Новый объект сертификата из DER */
static int pushcert(lua_State *L, const unsigned char *der, size_t len)
{
    t_cert *c = (t_cert *)lua_newuserdatauv(L, sizeof(t_cert), 0);
    mbedtls_x509_crt_init(&c->crt);
    luaL_setmetatable(L, LSSL_CERT);
    int ret = mbedtls_x509_crt_parse_der(&c->crt, der, len);
    if (ret != 0)
        return pusherror(L, "error parsing certificate", ret);
    return 1;
}

static int cert_load(lua_State *L)
{
    size_t len;
    const char *pem = luaL_checklstring(L, 1, &len);
    t_cert *c = (t_cert *)lua_newuserdatauv(L, sizeof(t_cert), 0);
    mbedtls_x509_crt_init(&c->crt);
    luaL_setmetatable(L, LSSL_CERT);
    /* mbedtls_x509_crt_parse ждёт PEM с завершающим нулём, длина — с ним */
    int ret = mbedtls_x509_crt_parse(&c->crt, (const unsigned char *)pem, len + 1);
    if (ret != 0)
        return pusherror(L, "error loading certificate", ret);
    return 1;
}

static int cert_destroy(lua_State *L)
{
    mbedtls_x509_crt_free(&checkcert(L, 1)->crt);
    return 0;
}

static int cert_pem(lua_State *L)
{
    t_cert *c = checkcert(L, 1);
    static const char hdr[] = "-----BEGIN CERTIFICATE-----\n";
    static const char ftr[] = "-----END CERTIFICATE-----\n";
    size_t olen = 0;
    mbedtls_pem_write_buffer(hdr, ftr, c->crt.raw.p, c->crt.raw.len, NULL, 0, &olen);
    luaL_Buffer b;
    unsigned char *out = (unsigned char *)luaL_buffinitsize(L, &b, olen);
    if (mbedtls_pem_write_buffer(hdr, ftr, c->crt.raw.p, c->crt.raw.len, out, olen, &olen) != 0)
    {
        lua_pushnil(L);
        return 1;
    }
    luaL_pushresultsize(&b, olen ? olen - 1 : 0); /* olen включает завершающий ноль */
    return 1;
}

static int cert_der(lua_State *L)
{
    t_cert *c = checkcert(L, 1);
    lua_pushlstring(L, (const char *)c->crt.raw.p, c->crt.raw.len);
    return 1;
}

static int pushname(lua_State *L, const mbedtls_x509_name *name)
{
    char buf[256];
    int n = mbedtls_x509_dn_gets(buf, sizeof(buf), name);
    if (n < 0)
        lua_pushnil(L);
    else
        lua_pushstring(L, buf);
    return 1;
}

static int cert_subject(lua_State *L)
{
    return pushname(L, &checkcert(L, 1)->crt.subject);
}

static int cert_issuer(lua_State *L)
{
    return pushname(L, &checkcert(L, 1)->crt.issuer);
}

static int pushtime(lua_State *L, const mbedtls_x509_time *t)
{
    lua_pushfstring(L, "%d-%02d-%02d %02d:%02d:%02d GMT", t->year, t->mon, t->day, t->hour, t->min, t->sec);
    return 1;
}

static int cert_notbefore(lua_State *L)
{
    return pushtime(L, &checkcert(L, 1)->crt.valid_from);
}

static int cert_notafter(lua_State *L)
{
    return pushtime(L, &checkcert(L, 1)->crt.valid_to);
}

static int cert_validat(lua_State *L)
{
    t_cert *c = checkcert(L, 1);
    /* Без аргумента — проверка на текущее время */
    lua_pushboolean(L, !mbedtls_x509_time_is_past(&c->crt.valid_to) &&
                           !mbedtls_x509_time_is_future(&c->crt.valid_from));
    return 1;
}

static int cert_serial(lua_State *L)
{
    char buf[80];
    int n = mbedtls_x509_serial_gets(buf, sizeof(buf), &checkcert(L, 1)->crt.serial);
    if (n < 0)
        lua_pushnil(L);
    else
        lua_pushstring(L, buf);
    return 1;
}

static int cert_info(lua_State *L)
{
    char buf[1024];
    int n = mbedtls_x509_crt_info(buf, sizeof(buf), "", &checkcert(L, 1)->crt);
    if (n < 0)
        lua_pushnil(L);
    else
        lua_pushstring(L, buf);
    return 1;
}

static int cert_eq(lua_State *L)
{
    t_cert *a = checkcert(L, 1), *b = checkcert(L, 2);
    lua_pushboolean(L, a->crt.raw.len == b->crt.raw.len &&
                           memcmp(a->crt.raw.p, b->crt.raw.p, a->crt.raw.len) == 0);
    return 1;
}

static int cert_tostring(lua_State *L)
{
    lua_pushfstring(L, "X509 certificate: %p", checkcert(L, 1));
    return 1;
}

static const luaL_Reg cert_methods[] = {
    {"pem", cert_pem},
    {"der", cert_der},
    {"subject", cert_subject},
    {"issuer", cert_issuer},
    {"notbefore", cert_notbefore},
    {"notafter", cert_notafter},
    {"validat", cert_validat},
    {"serial", cert_serial},
    {"info", cert_info},
    {NULL, NULL}};

static const luaL_Reg cert_meta[] = {
    {"__close", cert_destroy},
    {"__gc", cert_destroy},
    {"__eq", cert_eq},
    {"__tostring", cert_tostring},
    {NULL, NULL}};

static const luaL_Reg cert_funcs[] = {
    {"load", cert_load},
    {NULL, NULL}};

int luaopen_ssl_x509(lua_State *L)
{
    luaL_newmetatable(L, LSSL_CERT);
    luaL_setfuncs(L, cert_meta, 0);
    luaL_newlib(L, cert_methods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    luaL_newlib(L, cert_funcs);
    return 1;
}

/* ---------------------------------------------------------------------- */
/* Соединение (ssl.core)                                                  */
/* ---------------------------------------------------------------------- */

typedef struct
{
    t_socket sock;
    t_io io;
    t_buffer buf;
    t_timeout tm;
    mbedtls_ssl_context ssl;
    int state;
    int error; /* последний код Mbed TLS */
    int want;  /* 0, MBEDTLS_ERR_SSL_WANT_READ, MBEDTLS_ERR_SSL_WANT_WRITE */
    int alive; /* ssl ещё не освобождён */
} t_ssl;

static t_ssl *checkssl(lua_State *L, int idx)
{
    return (t_ssl *)luaL_checkudata(L, idx, LSSL_CONNECTION);
}

/* Транспорт TLS — сокет FreeRTOS+TCP напрямую: errno в newlib общий на все
 * задачи (нет NEWLIB_REENTRANT), а сокет здесь неблокирующий. */
static int bio_send(void *p, const unsigned char *buf, size_t len)
{
    t_ssl *ssl = (t_ssl *)p;
    BaseType_t ret = FreeRTOS_send((Socket_t)ssl->sock, buf, len, 0);
    if (ret > 0)
        return (int)ret;
    if (ret == 0 || ret == -pdFREERTOS_ERRNO_EWOULDBLOCK || ret == -pdFREERTOS_ERRNO_ENOSPC)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    if (ret == -pdFREERTOS_ERRNO_ENOTCONN)
        return MBEDTLS_ERR_NET_CONN_RESET;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int bio_recv(void *p, unsigned char *buf, size_t len)
{
    t_ssl *ssl = (t_ssl *)p;
    BaseType_t ret = FreeRTOS_recv((Socket_t)ssl->sock, buf, len, 0);
    if (ret > 0)
        return (int)ret;
    if (ret == 0 || ret == -pdFREERTOS_ERRNO_EWOULDBLOCK || ret == -pdFREERTOS_ERRNO_EINTR)
        return MBEDTLS_ERR_SSL_WANT_READ;
    if (ret == -pdFREERTOS_ERRNO_ENOTCONN)
        return 0; /* соединение закрыто */
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static const char *ssl_ioerror(void *ctx, int err)
{
    static char buf[96]; /* только для немедленного lua_pushstring */
    if (err != LSSL_IO_SSL)
        return socket_strerror(err);
    t_ssl *ssl = (t_ssl *)ctx;
    switch (ssl->error)
    {
    case 0:
        return "No error";
    case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
        return "closed";
    case MBEDTLS_ERR_SSL_WANT_READ:
        return "wantread";
    case MBEDTLS_ERR_SSL_WANT_WRITE:
        return "wantwrite";
    case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED:
    {
        uint32_t flags = mbedtls_ssl_get_verify_result(&ssl->ssl);
        int n = mbedtls_x509_crt_verify_info(buf, sizeof(buf), "", flags);
        if (n > 0 && buf[n - 1] == '\n')
            buf[n - 1] = 0;
        return n > 0 ? buf : "certificate verify failed";
    }
    default:
        mbedtls_strerror(ssl->error, buf, sizeof(buf));
        return buf;
    }
}

/* Ожидание сокета после WANT_READ/WANT_WRITE */
static int ssl_wait(t_ssl *ssl, int ret, p_timeout tm)
{
    ssl->want = ret;
    int err = socket_waitfd(&ssl->sock, ret == MBEDTLS_ERR_SSL_WANT_READ ? WAITFD_R : WAITFD_W, tm);
    if (err == IO_TIMEOUT)
        return LSSL_IO_SSL; /* как в LuaSec: "wantread"/"wantwrite" */
    return err;
}

static int handshake(t_ssl *ssl)
{
    p_timeout tm = timeout_markstart(&ssl->tm);
    if (ssl->state == LSSL_STATE_CLOSED)
        return IO_CLOSED;
    for (;;)
    {
        int ret = mbedtls_ssl_handshake(&ssl->ssl);
        ssl->error = ret;
        if (ret == 0)
        {
            ssl->want = 0;
            ssl->state = LSSL_STATE_CONNECTED;
            return IO_DONE;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            return LSSL_IO_SSL;
        int err = ssl_wait(ssl, ret, tm);
        if (err != IO_DONE)
            return err;
    }
}

static int ssl_send(void *ctx, const char *data, size_t count, size_t *sent, p_timeout tm)
{
    t_ssl *ssl = (t_ssl *)ctx;
    *sent = 0;
    if (ssl->state != LSSL_STATE_CONNECTED)
        return IO_CLOSED;
    for (;;)
    {
        int ret = mbedtls_ssl_write(&ssl->ssl, (const unsigned char *)data, count);
        ssl->error = ret < 0 ? ret : 0;
        if (ret >= 0)
        {
            ssl->want = 0;
            *sent = (size_t)ret;
            return IO_DONE;
        }
        if (ret == MBEDTLS_ERR_NET_CONN_RESET)
            return IO_CLOSED;
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            return LSSL_IO_SSL;
        int err = ssl_wait(ssl, ret, tm);
        if (err != IO_DONE)
            return err;
    }
}

static int ssl_recv(void *ctx, char *data, size_t count, size_t *got, p_timeout tm)
{
    t_ssl *ssl = (t_ssl *)ctx;
    *got = 0;
    if (ssl->state != LSSL_STATE_CONNECTED)
        return IO_CLOSED;
    for (;;)
    {
        int ret = mbedtls_ssl_read(&ssl->ssl, (unsigned char *)data, count);
        ssl->error = ret < 0 ? ret : 0;
        if (ret > 0)
        {
            ssl->want = 0;
            *got = (size_t)ret;
            return IO_DONE;
        }
        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == MBEDTLS_ERR_NET_CONN_RESET)
            return IO_CLOSED;
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            return LSSL_IO_SSL;
        int err = ssl_wait(ssl, ret, tm);
        if (err != IO_DONE)
            return err;
    }
}

static int meth_create(lua_State *L)
{
    t_context *ctx = checkcontext(L, 1);
    t_ssl *ssl = (t_ssl *)lua_newuserdatauv(L, sizeof(t_ssl), 1);
    memset(ssl, 0, sizeof(*ssl));
    ssl->sock = SOCKET_INVALID;
    ssl->state = LSSL_STATE_CLOSED;
    mbedtls_ssl_init(&ssl->ssl);
    ssl->alive = 1;
    luaL_setmetatable(L, LSSL_CONNECTION);
    /* Контекст должен жить не меньше соединения: mbedtls_ssl_context ссылается на conf */
    lua_pushvalue(L, 1);
    lua_setiuservalue(L, -2, 1);

    int ret = mbedtls_ssl_setup(&ssl->ssl, &ctx->conf);
    if (ret != 0)
        return pusherror(L, "error creating SSL object", ret);
    ssl->state = LSSL_STATE_NEW;
    io_init(&ssl->io, (p_send)ssl_send, (p_recv)ssl_recv, (p_error)ssl_ioerror, ssl);
    timeout_init(&ssl->tm, -1, -1);
    buffer_init(&ssl->buf, &ssl->io, &ssl->tm);
    return 1;
}

static int meth_destroy(lua_State *L)
{
    t_ssl *ssl = checkssl(L, 1);
    if (ssl->state == LSSL_STATE_CONNECTED)
        mbedtls_ssl_close_notify(&ssl->ssl); /* без ожидания: сокет неблокирующий */
    if (ssl->sock != SOCKET_INVALID)
        socket_destroy(&ssl->sock);
    if (ssl->alive)
        mbedtls_ssl_free(&ssl->ssl);
    ssl->alive = 0;
    ssl->state = LSSL_STATE_CLOSED;
    return 0;
}

static int meth_close(lua_State *L)
{
    meth_destroy(L);
    return 0;
}

static int meth_setfd(lua_State *L)
{
    t_ssl *ssl = checkssl(L, 1);
    if (ssl->state != LSSL_STATE_NEW)
        luaL_argerror(L, 1, "invalid SSL object state");
    ssl->sock = (t_socket)luaL_checkinteger(L, 2);
    socket_setnonblocking(&ssl->sock);
    mbedtls_ssl_set_bio(&ssl->ssl, ssl, bio_send, bio_recv, NULL);
    return 0;
}

static int meth_getfd(lua_State *L)
{
    lua_pushinteger(L, checkssl(L, 1)->sock);
    return 1;
}

static int meth_handshake(lua_State *L)
{
    t_ssl *ssl = checkssl(L, 1);
    int err = handshake(ssl);
    if (err == IO_DONE)
    {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    lua_pushstring(L, ssl_ioerror(ssl, err));
    return 2;
}

static int meth_send(lua_State *L)
{
    return buffer_meth_send(L, &checkssl(L, 1)->buf);
}

static int meth_receive(lua_State *L)
{
    return buffer_meth_receive(L, &checkssl(L, 1)->buf);
}

static int meth_getstats(lua_State *L)
{
    return buffer_meth_getstats(L, &checkssl(L, 1)->buf);
}

static int meth_setstats(lua_State *L)
{
    return buffer_meth_setstats(L, &checkssl(L, 1)->buf);
}

static int meth_settimeout(lua_State *L)
{
    return timeout_meth_settimeout(L, &checkssl(L, 1)->tm);
}

static int meth_dirty(lua_State *L)
{
    t_ssl *ssl = checkssl(L, 1);
    int res = 0;
    if (ssl->state != LSSL_STATE_CLOSED)
        res = !buffer_isempty(&ssl->buf) || mbedtls_ssl_get_bytes_avail(&ssl->ssl) > 0;
    lua_pushboolean(L, res);
    return 1;
}

static int meth_want(lua_State *L)
{
    t_ssl *ssl = checkssl(L, 1);
    const char *s = "nothing";
    if (ssl->state != LSSL_STATE_CLOSED)
    {
        if (ssl->want == MBEDTLS_ERR_SSL_WANT_READ)
            s = "read";
        else if (ssl->want == MBEDTLS_ERR_SSL_WANT_WRITE)
            s = "write";
    }
    lua_pushstring(L, s);
    return 1;
}

static int meth_sni(lua_State *L)
{
    t_ssl *ssl = checkssl(L, 1);
    const char *name = luaL_checkstring(L, 2);
    int ret = mbedtls_ssl_set_hostname(&ssl->ssl, name);
    lua_pushboolean(L, ret == 0);
    return 1;
}

static int meth_getsniname(lua_State *L)
{
    lua_pushnil(L); /* только для сервера */
    return 1;
}

static int meth_getalpn(lua_State *L)
{
    const char *p = mbedtls_ssl_get_alpn_protocol(&checkssl(L, 1)->ssl);
    if (p)
        lua_pushstring(L, p);
    else
        lua_pushnil(L);
    return 1;
}

static int meth_getpeercertificate(lua_State *L)
{
    t_ssl *ssl = checkssl(L, 1);
    int n = (int)luaL_optinteger(L, 2, 1);
    const mbedtls_x509_crt *crt = NULL;
    if (ssl->state == LSSL_STATE_CONNECTED)
        crt = mbedtls_ssl_get_peer_cert(&ssl->ssl);
    while (crt != NULL && --n > 0)
        crt = crt->next;
    if (crt == NULL || crt->raw.len == 0)
    {
        lua_pushnil(L);
        return 1;
    }
    return pushcert(L, crt->raw.p, crt->raw.len);
}

static int meth_getpeerchain(lua_State *L)
{
    t_ssl *ssl = checkssl(L, 1);
    const mbedtls_x509_crt *crt = NULL;
    if (ssl->state == LSSL_STATE_CONNECTED)
        crt = mbedtls_ssl_get_peer_cert(&ssl->ssl);
    if (crt == NULL)
    {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    for (int i = 1; crt != NULL && crt->raw.len; crt = crt->next, i++)
    {
        if (pushcert(L, crt->raw.p, crt->raw.len) != 1)
            return 2;
        lua_rawseti(L, -2, i);
    }
    return 1;
}

/* getpeerverification() -> true | false, {"причина"} */
static int meth_getpeerverification(lua_State *L)
{
    t_ssl *ssl = checkssl(L, 1);
    if (ssl->state != LSSL_STATE_CONNECTED)
    {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "closed");
        return 2;
    }
    uint32_t flags = mbedtls_ssl_get_verify_result(&ssl->ssl);
    if (flags == 0)
    {
        lua_pushboolean(L, 1);
        return 1;
    }
    char buf[256];
    int n = mbedtls_x509_crt_verify_info(buf, sizeof(buf), "", flags);
    lua_pushboolean(L, 0);
    lua_newtable(L);
    lua_pushstring(L, n > 0 ? buf : "verify failed");
    lua_rawseti(L, -2, 1);
    return 2;
}

static int meth_tostring(lua_State *L)
{
    lua_pushfstring(L, "SSL connection: %p%s", checkssl(L, 1),
                    checkssl(L, 1)->state == LSSL_STATE_CLOSED ? " (closed)" : "");
    return 1;
}

/* core.setmethod(name, func) — так ssl.lua добавляет метод info */
static int meth_setmethod(lua_State *L)
{
    luaL_getmetatable(L, LSSL_CONNECTION);
    lua_pushstring(L, "__index");
    lua_gettable(L, -2);
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_settable(L, -3);
    return 0;
}

/* core.info(ssl) -> "SUITE VERSION Kx=.. Au=.. Enc=.. Mac=..", bits, algbits, protocol */
static int meth_info(lua_State *L)
{
    t_ssl *ssl = checkssl(L, 1);
    if (ssl->state != LSSL_STATE_CONNECTED)
        return 0;
    const char *suite = mbedtls_ssl_get_ciphersuite(&ssl->ssl);
    const char *ver = mbedtls_ssl_get_version(&ssl->ssl);
    const char *au = strstr(suite, "ECDSA") ? "ECDSA" : "RSA";
    const char *enc = strstr(suite, "CHACHA20") ? "CHACHA20/POLY1305(256)"
                      : strstr(suite, "AES-256") ? "AESGCM(256)"
                                                 : "AESGCM(128)";
    int bits = strstr(suite, "AES-128") ? 128 : 256;
    lua_pushfstring(L, "%s %s Kx=ECDH Au=%s Enc=%s Mac=AEAD", suite, ver, au, enc);
    lua_pushinteger(L, bits);
    lua_pushinteger(L, bits);
    lua_pushstring(L, ver);
    return 4;
}

static int meth_compression(lua_State *L)
{
    checkssl(L, 1);
    lua_pushnil(L);
    return 1;
}

static int meth_copyright(lua_State *L)
{
    lua_pushstring(L, "LuaSec 1.3.2 API on Mbed TLS - KitsLab; LuaSec (C) 2006-2023 Bruno Silvestre, UFG");
    return 1;
}

static const luaL_Reg ssl_methods[] = {
    {"close", meth_close},
    {"dirty", meth_dirty},
    {"dohandshake", meth_handshake},
    {"getalpn", meth_getalpn},
    {"getfd", meth_getfd},
    {"getpeercertificate", meth_getpeercertificate},
    {"getpeerchain", meth_getpeerchain},
    {"getpeerverification", meth_getpeerverification},
    {"getsniname", meth_getsniname},
    {"getstats", meth_getstats},
    {"receive", meth_receive},
    {"send", meth_send},
    {"setstats", meth_setstats},
    {"settimeout", meth_settimeout},
    {"sni", meth_sni},
    {"want", meth_want},
    {NULL, NULL}};

static const luaL_Reg ssl_meta[] = {
    {"__close", meth_destroy},
    {"__gc", meth_destroy},
    {"__tostring", meth_tostring},
    {NULL, NULL}};

static const luaL_Reg ssl_funcs[] = {
    {"compression", meth_compression},
    {"create", meth_create},
    {"info", meth_info},
    {"setfd", meth_setfd},
    {"setmethod", meth_setmethod},
    {"copyright", meth_copyright},
    {NULL, NULL}};

int luaopen_ssl_core(lua_State *L)
{
    luaL_newmetatable(L, LSSL_CONNECTION);
    luaL_setfuncs(L, ssl_meta, 0);
    luaL_newlib(L, ssl_methods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
    luaL_newlib(L, ssl_funcs);
    lua_pushinteger(L, SOCKET_INVALID);
    lua_setfield(L, -2, "SOCKET_INVALID");
    return 1;
}

/* ---------------------------------------------------------------------- */
/* Возможности сборки (ssl.config)                                        */
/* ---------------------------------------------------------------------- */

static void setbool(lua_State *L, const char *name)
{
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, name);
}

int luaopen_ssl_config(lua_State *L)
{
    lua_newtable(L);

    lua_newtable(L); /* options: принимаются любые, см. ctx_accept */
    lua_setfield(L, -2, "options");

    lua_newtable(L);
    setbool(L, "tlsv1_2");
    lua_setfield(L, -2, "protocols");

    lua_newtable(L);
    setbool(L, "ec");
    lua_setfield(L, -2, "algorithms");

    lua_newtable(L);
    setbool(L, "P-256");
    setbool(L, "P-384");
    setbool(L, "X25519");
    lua_setfield(L, -2, "curves");

    lua_newtable(L);
    setbool(L, "alpn");
    setbool(L, "curves_list");
    lua_setfield(L, -2, "capabilities");

    return 1;
}

