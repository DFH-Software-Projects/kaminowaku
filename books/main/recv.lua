-- Copyright 2026 Jamison A. Drapeau
local result = require("kami.result")
local recv = {}
local pending = ""

local function positive_integer(value, name)
        if type(value) ~= "number" or value % 1 ~= 0 or value <= 0 then
                error(name .. " must be a positive integer", 3)
        end
        return value
end

local function optional_timeout(value)
        if value == nil then return nil end
        return positive_integer(value, "timeout_ms")
end

local function require_stream(name)
        local info = _kami.session_info()
        if info and info.transport == "udp" then
                error(name .. " is stream-only and cannot consume UDP datagrams", 3)
        end
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

local function receive_notice(data)
        if data and #data > 0 then
                result.complete(
                        endpoint()
                        .. " received "
                        .. tostring(#data)
                        .. " bytes."
                )
        end
end

local function raw_receive(max_bytes, timeout_ms)
        if timeout_ms then return _kami.rx(max_bytes, timeout_ms) end
        return _kami.rx(max_bytes)
end

local function pending_take(max_bytes)
        local length
        local data

        if #pending == 0 then return nil end

        length = #pending
        if length > max_bytes then length = max_bytes end

        data = string.sub(pending, 1, length)
        pending = string.sub(pending, length + 1)
        return data
end

-- TCP: returns up to max_bytes of stream data.
-- UDP: returns exactly one datagram, or its bounded prefix with status "limit".
function recv.bytes(max_bytes, timeout_ms)
        local data
        local status

        max_bytes = positive_integer(max_bytes, "max_bytes")
        timeout_ms = optional_timeout(timeout_ms)

        data = pending_take(max_bytes)
        if data then
                receive_notice(data)
                return data, "ok"
        end

        data, status = raw_receive(max_bytes, timeout_ms)
        receive_notice(data)
        return data, status
end

function recv.exact(count, timeout_ms)
        local prefix = ""
        local data
        local status
        local combined

        require_stream("recv.exact")
        count = positive_integer(count, "count")
        timeout_ms = optional_timeout(timeout_ms)

        if #pending > 0 then
                if #pending >= count then
                        data = string.sub(pending, 1, count)
                        pending = string.sub(pending, count + 1)
                        receive_notice(data)
                        return data, "ok"
                end

                prefix = pending
                pending = ""
        end

        if timeout_ms then data, status = _kami.rx_exact(count - #prefix, timeout_ms)
        else data, status = _kami.rx_exact(count - #prefix) end

        if data then combined = prefix .. data
        elseif #prefix > 0 then combined = prefix
        else combined = nil end

        receive_notice(combined)
        return combined, status
end

recv["until"] = function(marker, max_bytes, timeout_ms)
        local buffer = pending
        local marker_start
        local marker_end

        require_stream("recv[\"until\"]")
        if type(marker) ~= "string" or #marker == 0 then
                error("marker must be a non-empty string", 2)
        end
        max_bytes = positive_integer(max_bytes, "max_bytes")
        timeout_ms = optional_timeout(timeout_ms)
        pending = ""

        marker_start, marker_end = string.find(buffer, marker, 1, true)
        if marker_end then
                local data = string.sub(buffer, 1, marker_end)
                pending = string.sub(buffer, marker_end + 1)
                receive_notice(data)
                return data, "ok"
        end

        if #buffer >= max_bytes then
                local data = string.sub(buffer, 1, max_bytes)
                pending = string.sub(buffer, max_bytes + 1)
                receive_notice(data)
                return data, "limit"
        end

        while #buffer < max_bytes do
                local remaining = max_bytes - #buffer
                local chunk_size = remaining
                local data
                local status

                if chunk_size > 4096 then chunk_size = 4096 end
                data, status = raw_receive(chunk_size, timeout_ms)

                if data then
                        buffer = buffer .. data

                        marker_start, marker_end = string.find(
                                buffer,
                                marker,
                                1,
                                true
                        )

                        if marker_end then
                                local matched = string.sub(buffer, 1, marker_end)
                                pending = string.sub(buffer, marker_end + 1)
                                receive_notice(matched)
                                return matched, "ok"
                        end
                end

                if #buffer >= max_bytes then
                        local bounded = string.sub(buffer, 1, max_bytes)
                        pending = string.sub(buffer, max_bytes + 1)
                        receive_notice(bounded)
                        return bounded, "limit"
                end

                if status ~= "ok" then
                        local partial = (#buffer > 0) and buffer or nil
                        receive_notice(partial)
                        return partial, status
                end
        end

        local partial = (#buffer > 0) and buffer or nil
        receive_notice(partial)
        return partial, "limit"
end

return recv
