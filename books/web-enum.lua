-- Copyright 2026 Jamison A. Drapeau
-- web-enum.lua - HTTPS service enumeration Book.
book {
        name = "web-enum"
}

local http = require("modules.http")
local result = require("kami.result")

local port = tonumber(args.port or "443")
if not port or port < 1 or port > 65535 or port % 1 ~= 0 then
        result.bail("Invalid --port: expected an integer from 1 to 65535")
end

local path = args.path or "/"
if string.sub(path, 1, 1) ~= "/" or string.find(path, "\r", 1, true)
        or string.find(path, "\n", 1, true) then
        result.bail("Invalid --path: expected a path beginning with /")
end

local use_tls = true
if args.tls ~= nil then
        if args.tls == "true" then
                use_tls = true
        elseif args.tls == "false" then
                use_tls = false
        else
                result.bail("Invalid --tls: expected true or false")
        end
end

result.notice("Starting HTTP service enumeration.")

local response, err = http.request({
        port = port,
        tls = use_tls,
        path = path
})
if not response then
        result.bail(err or "HTTPS enumeration failed")
end

result.emit("SCHEME", response.scheme)
result.emit("STATUS", tostring(response.status))
result.emit("HTTP-VERSION", response.version)
result.emit("PORT", tostring(response.port))

if response.headers["server"] then
        result.emit("SERVER", response.headers["server"])
end

if response.headers["content-type"] then
        result.emit("CONTENT-TYPE", response.headers["content-type"])
end

if response.headers["location"] then
        result.emit("LOCATION", response.headers["location"])
end

if response.headers["www-authenticate"] then
        result.emit("AUTH", response.headers["www-authenticate"])
end

if response.body and #response.body > 0 then
        result.emit("BODY-SAMPLE-BYTES", tostring(#response.body))
end
