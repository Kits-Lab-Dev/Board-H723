#ifndef TERM_H
#define TERM_H

#include "stdint.h"
#include <stddef.h>

#define TERM_CFG_CMDLINE_LEN 4096
#define TERM_CFG_ARG_COUNT 32
#define TERM_CFG_RING_HISTORY_LEN 16
#define TERM_PROMPT_LEN 128
#define TERM_CWD_LEN 512
#define TERM_FULL_PATH_LEN 768



#define TERM_COLOR_RED                     "\033[31m"
#define TERM_COLOR_GREEN                   "\033[32m"
#define TERM_COLOR_YELLOW                  "\033[33m"
#define TERM_COLOR_BLUE                    "\033[34m"
#define TERM_COLOR_PURPLE                  "\033[35m"
#define TERM_COLOR_MAGENTA                 "\033[36m"
#define TERM_COLOR_WHITE                   "\033[37m"
#define TERM_COLOR_DEFAULT                 "\033[0m"

struct term_t;

typedef int (*term_output_fn)(struct term_t *trm, const char *str);
typedef int (*term_read_fn)(struct term_t *trm, char *c, size_t n);
typedef int (*term_exec_fn)(struct term_t *trm, int argc, const char *const *argv);
typedef int (*term_newline_fn)(struct term_t *trm, const char *line);
typedef char **(*term_get_compl_fn)(struct term_t *trm, int argc, const char *const *argv);
typedef void (*term_sigint_fn)(struct term_t *trm);

typedef enum
{
    termOK = 0x00,        
    termERR = 0x01,      
    termERRPAR = 0x02,    
    termERRTKNNUM = 0x03, 
    termERRCLFULL = 0x04, 
    termERRCPLT = 0x05    
} termr_t;

typedef enum
{
    term_ESC_NULL,     
    term_ESC_BRACKET, 
    term_ESC_HOME,    
    term_ESC_END,     
    term_ESC_DEL      
} term_esc_code_t;

typedef enum
{
    term_ECHO_ONCE, 
    term_ECHO_ON,   
    term_ECHO_OFF   
} term_echo_t;

struct term_hist_rbuf;

typedef struct {
    char cmdline[TERM_CFG_CMDLINE_LEN];
    size_t len;
} term_hist_entry_t;

typedef struct term_hist_rbuf {
    term_hist_entry_t entries[TERM_CFG_RING_HISTORY_LEN];
    int32_t head;
    int32_t tail;
    int32_t count;
} term_hist_rbuf_t;

typedef struct term_t {
    term_output_fn output; 
    term_read_fn read;                        
    term_exec_fn exec;                          
    term_newline_fn newline;                    
    term_get_compl_fn get_compl;               
    term_sigint_fn sigint;                      
    char cmdline_str[TERM_CFG_CMDLINE_LEN + 1]; 
    int32_t cmdlen;                             
    int32_t cursor;                              
    char last_endl;                             
    term_esc_code_t esc_code;                   
    uint8_t escape;                            
    term_hist_rbuf_t ring_hist;                 
    term_echo_t echo;                       
    int32_t echo_off_pos;                     
    char cwd[TERM_CWD_LEN];      
    char fullPathBuffer[TERM_FULL_PATH_LEN];                      
    char prompt[TERM_PROMPT_LEN];
    int32_t hist_pos;                         
    void *userdata_ptr;                        
}term_t;

termr_t term_init(term_t *trm, term_output_fn out_fn, term_read_fn read_fn, term_exec_fn exec_fn);
termr_t term_set_execute_callback(term_t *trm, term_exec_fn exec_fn);
termr_t term_set_complete_callback(term_t *trm, term_get_compl_fn get_completion_fn);
termr_t term_set_sigint_callback(term_t *trm, term_sigint_fn sigint_fn);
termr_t term_set_prompt(term_t *trm, const char *username, const char *hostname);
termr_t term_set_echo(term_t *trm, term_echo_t echo);
termr_t term_processing_input(term_t *trm, const void *data_ptr, size_t len);
char* term_get_full_path(term_t *trm, const char *path);
uint32_t term_get_version(void);

termr_t term_set_cwd(term_t *trm, const char *cwd);
void term_print_prompt(term_t *trm);

#endif