-- Copyright 2026 Jamison A. Drapeau
local result = require("kami.result")
local send = {}

local function payload(value)
        if type(value) ~= "string" then
                error("send payload must be a string", 3)
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

local function endpoint()
        local info = _kami.session_info()
        local transport = "NET"

        if info then
                if info.transport == "tcp" then transport = "TCP"
                elseif info.transport == "udp" then transport = "UDP" end

                if info.remote_port and info.remote_port > 0 then
                        if info.tls and transport == "TCP" then
                                return "TLS/TCP/" .. tostring(info.remote_port)
                        end
                        return transport .. "/" .. tostring(info.remote_port)
                end
        end

        return transport
end

local function transmit(data, timeout_ms)
        local written, status

        if timeout_ms then written, status = _kami.tx(data, timeout_ms)
        else written, status = _kami.tx(data) end

        if written and written > 0 then
                result.inquiry(
                        endpoint()
                        .. " sent "
                        .. tostring(written)
                        .. " bytes."
                )
        end

        return written, status
end

function send.bytes(data, timeout_ms)
        data = payload(data)
        timeout_ms = timeout(timeout_ms)
        return transmit(data, timeout_ms)
end

function send.text(text, timeout_ms)
        text = payload(text)
        timeout_ms = timeout(timeout_ms)
        return transmit(text, timeout_ms)
end

return send
