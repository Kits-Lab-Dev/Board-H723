local M = {}

function M.boot()
    wifi.enable(true)
    -- wifi.setPowerSaveMode(WIFI_PS_NONE)
    wifi.setMode(WIFI_STA)
    wifi.connect('ap_name', 'password')

    while true do
        thread.delay(3000)
    end
end

return M
