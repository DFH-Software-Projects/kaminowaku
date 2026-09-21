-- Copyright 2026 Jamison A. Drapeau
local transport = {}

local function port(value)
        if type(value) ~= "number" or value % 1 ~= 0 or value < 1 or value > 65535 then
                error("port must be an integer between 1 and 65535", 3)
        end
        return value
end

local function timeout(value)
        if value == nil then return nil end
        if type(value) ~= "number" or value % 1 ~= 0 or value <= 0 then
                error("timeout_ms must be a positive integer", 3)
        end
        return value
end

function transport.tcp(port_number, timeout_ms)
        port_number = port(port_number)
        timeout_ms = timeout(timeout_ms)
        if timeout_ms then return _kami.tcp_open(port_number, timeout_ms) end
        return _kami.tcp_open(port_number)
end

function transport.udp(port_number, timeout_ms)
        port_number = port(port_number)
        timeout_ms = timeout(timeout_ms)
        if timeout_ms then return _kami.udp_open(port_number, timeout_ms) end
        return _kami.udp_open(port_number)
end

function transport.info()
        return _kami.session_info()
end

return transport
