-- Copyright 2026 Jamison A. Drapeau
local recv = require("kami.recv")
local send = require("kami.send")
local tls = require("kami.tls")
local transport = require("kami.transport")

local http = {}

local HEADER_LIMIT = 32768
local BODY_SAMPLE_LIMIT = 32768

local function split_lines(text)
        local lines = {}
        for line in string.gmatch(text, "([^\r\n]+)") do
                lines[#lines + 1] = line
        end
        return lines
end

local function parse_headers(block)
        local lines = split_lines(block)
        if #lines == 0 then return nil, "empty HTTP response" end

        local response = {
                status_line = lines[1],
                headers = {},
                body = ""
        }

        local version, code, reason = string.match(
                lines[1],
                "^(HTTP/%d+%.%d+)%s+(%d%d%d)%s*(.*)$"
        )
        if not version then return nil, "invalid HTTP status line" end

        response.version = version
        response.status = tonumber(code)
        response.reason = reason

        for i = 2, #lines do
                local key, value = string.match(lines[i], "^([^:]+):%s*(.*)$")
                if key then
                        local lower = string.lower(key)
                        if response.headers[lower] == nil then
                                response.headers[lower] = value
                        else
                                response.headers[lower] = response.headers[lower] .. ", " .. value
                        end
                end
        end

        return response, nil
end

local function target_host()
        if target.host and target.host ~= "" then return target.host end
        if target.ipv4 and target.ipv4 ~= "" then return target.ipv4 end
        if target.ipv6 and target.ipv6 ~= "" then return "[" .. target.ipv6 .. "]" end
        return ""
end

function http.request(options)
        options = options or {}

        local port = options.port or 80
        local method = options.method or "GET"
        local path = options.path or "/"
        local timeout_ms = options.timeout_ms
        local secure = options.tls
        local host = options.host or target_host()
        local tls_active = false

        if secure == nil then secure = (port == 443) end
        if type(secure) ~= "boolean" then return nil, "tls option must be boolean" end
        if host == "" then return nil, "target has no HTTP host identity" end

        local opened, open_status = transport.tcp(port, timeout_ms)
        if not opened then return nil, "TCP connect failed: " .. tostring(open_status) end

        local function fail(message)
                if tls_active then
                        tls.close()
                        tls_active = false
                end
                return nil, message
        end

        if secure then
                local server_name = host
                if string.sub(server_name, 1, 1) == "[" and string.sub(server_name, -1) == "]" then
                        server_name = string.sub(server_name, 2, -2)
                end

                local tls_opened, tls_status = tls.open(server_name, timeout_ms)
                if not tls_opened then
                        return fail("TLS handshake failed: " .. tostring(tls_status))
                end
                tls_active = true
        end

        local request = method .. " " .. path .. " HTTP/1.1\r\n"
                .. "Host: " .. host .. "\r\n"
                .. "User-Agent: Kaminowaku/0.0.9\r\n"
                .. "Accept: */*\r\n"
                .. "Connection: close\r\n\r\n"

        local written, write_status = send.text(request, timeout_ms)
        if not written then return fail("HTTP request send failed: " .. tostring(write_status)) end

        local header_block, header_status = recv["until"]("\r\n\r\n", HEADER_LIMIT, timeout_ms)
        if not header_block then
                return fail("HTTP header receive failed: " .. tostring(header_status))
        end

        local response, parse_error = parse_headers(header_block)
        if not response then return fail(parse_error) end

        local body, body_status = recv.bytes(BODY_SAMPLE_LIMIT, timeout_ms)
        if body then response.body = body end
        response.body_status = body_status
        response.port = port
        response.tls = secure
        response.scheme = secure and "https" or "http"

        if tls_active then
                tls.close()
                tls_active = false
        end

        return response, nil
end

return http
