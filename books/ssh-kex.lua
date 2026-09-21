-- Copyright 2026 Jamison A. Drapeau
-- ssh-kex.lua - Enumerate SSH transport identification and KEXINIT algorithms.
book {
        name = "ssh-kex"
}

local ssh = require("modules.ssh")
local result = require("kami.result")

result.notice("Starting SSH key-exchange enumeration.")

local kex, err = ssh.kex()
if not kex then
        result.bail(err or "SSH key-exchange enumeration failed")
end

result.emit("BANNER", kex.banner)
result.emit("PORT", tostring(kex.port))

for _, algorithm in ipairs(kex.kex) do
        if algorithm == "diffie-hellman-group14-sha1" then
                result.emit("KEX", algorithm, "yellow")
        else
                result.emit("KEX", algorithm)
        end
end

for _, algorithm in ipairs(kex.hostkeys) do
        if algorithm == "ssh-rsa" then
                result.emit("HOSTKEY", algorithm, "yellow")
        else
                result.emit("HOSTKEY", algorithm)
        end
end

for _, algorithm in ipairs(kex.ciphers_c2s) do
        result.emit("CIPHER-C2S", algorithm)
end

for _, algorithm in ipairs(kex.ciphers_s2c) do
        result.emit("CIPHER-S2C", algorithm)
end

for _, algorithm in ipairs(kex.macs_c2s) do
        result.emit("MAC-C2S", algorithm)
end

for _, algorithm in ipairs(kex.macs_s2c) do
        result.emit("MAC-S2C", algorithm)
end

for _, algorithm in ipairs(kex.compression_c2s) do
        result.emit("COMPRESSION-C2S", algorithm)
end

for _, algorithm in ipairs(kex.compression_s2c) do
        result.emit("COMPRESSION-S2C", algorithm)
end
