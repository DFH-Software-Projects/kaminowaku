-- Copyright 2026 Jamison A. Drapeau
-- web-enum.lua - HTTPS service enumeration Book.
book {
        name = "web-enum"
}

local http = require("modules.http")
local result = require("kami.result")

result.notice("Starting HTTPS service enumeration.")

local response, err = http.request({
        port = 443,
        tls = true,
        path = "/"
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
