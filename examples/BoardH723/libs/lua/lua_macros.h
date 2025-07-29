#ifndef LUA_MACROS_H
#define LUA_MACROS_H

#define LUA_OUT
#define LUA_PATH_DEFAULT "?.lua;" "/lib/?.lua;" "/ram/lib/?.lua;" "/sd/lib/?.lua;" "/sd/?.lua"
#define lua_lock(x) luaLock(x)
#define lua_unlock(x) luaUnlock(x)
#define lua_writestring(s,l) LUA_PRINT(s, l)
#define lua_writeline() lua_writestring("\r\n", 2)
#define lua_writestringerror(s,p) ERR(s,p)
#define luai_userstatethread(L,L1) LUA_USERSTATETHREAD(L,L1)
#define luai_userstatefree(L,L1) LUA_USERSTATEFREE(L,L1)

#endif /* LUA_MACROS_H */ 