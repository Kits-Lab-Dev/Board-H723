
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define AF_UNIX     1       /* local to host (pipes) */

struct sockaddr_un {
    short                    sun_family;                /*AF_UNIX*/
    char                     sun_path[108];             /*path name */
};

#ifdef __cplusplus
}
#endif
