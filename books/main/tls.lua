-- Copyright 2026 Jamison A. Drapeau
local tls = {}

local function timeout(value)
        if value == nil then return nil end
        if type(value) ~= "number" or value % 1 ~= 0 or value <= 0 then
                error("timeout_ms must be a positive integer", 3)
        end
        return value
end

function tls.open(server_name, timeout_ms)
        if server_name ~= nil and type(server_name) ~= "string" then
                error("server_name must be a string", 2)
        end
        server_name = server_name or ""
        timeout_ms = timeout(timeout_ms)
        if timeout_ms then return _kami.tls_open(server_name, timeout_ms) end
        return _kami.tls_open(server_name)
end

function tls.close()
        return _kami.tls_close()
end

function tls.info()
        return _kami.tls_info()
end

return tls
