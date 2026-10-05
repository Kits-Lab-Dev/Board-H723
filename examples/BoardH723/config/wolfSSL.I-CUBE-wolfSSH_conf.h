/**
  ******************************************************************************
  * File Name          : wolfSSL.I-CUBE-wolfSSH_conf.h
  * Description        : This file provides code for the configuration
  *                      of the wolfSSL.I-CUBE-wolfSSH_conf.h instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __WOLFSSL_I_CUBE_WOLFSSH_CONF_H__
#define __WOLFSSL_I_CUBE_WOLFSSH_CONF_H__

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

/**
	MiddleWare name : wolfSSL.I-CUBE-wolfSSH.1.4.17
	MiddleWare fileName : wolfSSL.I-CUBE-wolfSSH_conf.h
	MiddleWare version :
*/
/*---------- WOLFSSH_CONF_SFTP -----------*/
#define WOLFSSH_CONF_SFTP     1

/*---------- WOLFSSH_CONF_SCP -----------*/
#define WOLFSSH_CONF_SCP      0

/*---------- WOLFSSH_CONF_DEBUG -----------*/
#define WOLFSSH_CONF_DEBUG    0

/*---------- WOLFSSH_CONF_IO -----------*/
#define WOLFSSH_CONF_IO       1

#define NO_TIMESTAMP
#define WOLFSSH_KEYGEN
#define WOLFSSL_KEY_GEN
#define WOLFSSH_SMALL_STACK
// #define WOLFSSH_FATFS
// #define WOLFSSH_STOREHANDLE
// #define WOLFSSH_SHELL

#define WOLFSSH_MAX_SFTP_RW 2048
#define WOLFSSH_MAX_SFTP_RECV 2048
#define DEFAULT_MAX_PACKET_SZ 2048
#define MAX_PACKET_SZ 4096
#define DEFAULT_WINDOW_SZ 16384

#include "sys/errno.h"
#include "dirent.h"
#include "time.h"
// #define XGMTIME(c, t)   gmtime((c))

// #define WMALLOC_USER

#ifdef WMALLOC_USER
#include "sdram.h"
#include <wolfssl/wolfcrypt/types.h>

    #define WMALLOC(s, h, t)      sdramMalloc(s)
    #define WFREE(p, h, t)        sdramFree(p)
    #define WREALLOC(p, n, h, t)  sdramRealloc(p, n)
    // #define wolfSSH_SetAllocators wolfSSL_SetAllocators
#endif
/* ------------------------------------------------------------------------- */
/* Platform */
/* ------------------------------------------------------------------------- */
#define WOLFSSH_STM32_CUBEMX
/* #define WOLFSSL_CMSIS_RTOS */
/* #define NO_FILESYSTEM */

/* ------------------------------------------------------------------------- */
/* Enable Features */
/* ------------------------------------------------------------------------- */
/* SCP */
#undef WOLFSSH_SCP
#if defined(WOLFSSH_CONF_SCP) && WOLFSSH_CONF_SCP == 1
	#define WOLFSSH_SCP
#endif

/* SFTP */
#undef WOLFSSH_SFTP
#if !defined(NO_FILESYSTEM) && defined(WOLFSSH_CONF_SFTP) && WOLFSSH_CONF_SFTP == 1
	#define WOLFSSH_SFTP
#endif

/* ------------------------------------------------------------------------- */
/* Debugging */
/* ------------------------------------------------------------------------- */
#if defined(WOLFSSH_CONF_DEBUG) && WOLFSSH_CONF_DEBUG == 1
    #define DEBUG_WOLFSSH
#endif

/* ------------------------------------------------------------------------- */
/* wolfSSH IO */
/* ------------------------------------------------------------------------- */
#if defined(WOLFSSH_CONF_IO) && WOLFSSH_CONF_IO == 2
    #define WOLFSSH_LWIP
#else
    #define WOLFSSH_USER_IO
#endif

/* To be defined for the target Socket API */
#define WSTARTTCP()

#define WOLFSSH_LOG_PRINTF
#define WOLFSSL_LOG_PRINTF
#define fprintf(err, ... ) DBG(__VA_ARGS__)
#define WFFLUSH fflush

#define BENCH_EMBEDDED
#define NO_WRITEV
#define NO_DEV_RANDOM
#define USE_CERT_BUFFERS_2048
#define WOLFSSL_USER_CURRTIME
#define SIZEOF_LONG_LONG 8
#define NO_WOLFSSL_DIR
#define WOLFSSL_NO_CURRDIR
#define NO_WOLF_C99
#define NO_MULTIBYTE_PRINT

#if !defined(NO_FILESYSTEM)
    #define WOLFSSH_USER_FILESYSTEM
#endif
// #define NO_WOLFSSH_DIR
#define WOLFSSH_SHELL
// #define WOLFSSH_TERM
// #define NO_TERMIOS

#define XVALIDATEDATE(d, f,t) (0)
#define WOLFSSL_USER_CURRTIME /* for benchmark */

#define WOLFSSL_GENSEED_FORTEST /* Warning: define your own seed gen */

#define TFM_TIMING_RESISTANT
#define ECC_TIMING_RESISTANT
#define WC_RSA_BLINDING

/* #define NO_DH */
#define HAVE_AESGCM
#define WOLFSSL_SHA512
#define HAVE_ECC
#define HAVE_ED25519

#define WOLFSSH_IGNORE_FILE_WARN

// #define WOLFSSH_AGENT

typedef unsigned int size_t;

/* defines for unit tests */
#define NO_UNITTEST_MAIN_DRIVER
#define NO_TESTSUITE_MAIN_DRIVER
#define NO_APITEST_MAIN_DRIVER

#ifdef __cplusplus
}
#endif
#endif /* WOLFSSL_I_CUBE_WOLFSSH_CONF_H_H */

/**
  * @}
  */

/*****END OF FILE****/
