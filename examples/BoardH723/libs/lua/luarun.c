const char *mainlua = " \
local rtos = require 'rtos' \
local thread = require 'thread' \
local b = require 'boot'\r\n  \
function boot() \r\n  \
    print('booting...')\r\n  \
    b.boot();\r\n  \
end \r\n \
local res, err = pcall(boot)\r\n \
if not res then\r\n \
    print(err)\r\n \
end\r\n \
while true do\r\n  \
    thread.delay(1000)\r\n  \
end "\
;