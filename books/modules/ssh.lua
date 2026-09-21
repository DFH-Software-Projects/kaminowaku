-- Copyright 2026 Jamison A. Drapeau
-- RFC 4253 SSH transport identification and KEXINIT enumeration.
local bytes = require("kami.bytes")
local recv = require("kami.recv")
local result = require("kami.result")
local send = require("kami.send")
local transport = require("kami.transport")

local ssh = {}

local SSH_PORT = 22
local SSH_MSG_KEXINIT = 20
local IDENTIFICATION_LIMIT = 512
local IDENTIFICATION_LINES = 8
local PACKET_LIMIT = 35000

local function ssh_string(value)
        return bytes.pack_u32be(#value) .. value
end

local function name_list(values)
        return ssh_string(table.concat(values, ","))
end

local function random_bytes(count)
        local out = {}
        for i = 1, count do
                out[i] = string.char(math.random(0, 255))
        end
        return table.concat(out)
end

local function clean_identification(line)
        return string.gsub(line, "\r?\n$", "")
end

local function send_complete(data, timeout_ms)
        local written, status = send.bytes(data, timeout_ms)
        if status ~= "ok" then
                return nil, status
        end
        if not written or written ~= #data then
                return nil, "short-write"
        end
        return written, "ok"
end

local function server_identification(timeout_ms)
        for _ = 1, IDENTIFICATION_LINES do
                local line, status = recv["until"]("\n", IDENTIFICATION_LIMIT, timeout_ms)
                if status ~= "ok" then
                        return nil, "SSH identification receive failed: " .. tostring(status)
                end
                if not line then
                        return nil, "SSH identification receive failed: empty response"
                end

                line = clean_identification(line)
                if string.sub(line, 1, 4) == "SSH-" then
                        if string.sub(line, 1, 8) ~= "SSH-2.0-" and string.sub(line, 1, 9) ~= "SSH-1.99-" then
                                return nil, "remote endpoint does not advertise SSH protocol 2"
                        end
                        return line, "ok"
                end
        end

        return nil, "SSH identification not found within bounded pre-banner lines"
end

local function client_kexinit_payload()
        local kex = {
                "curve25519-sha256",
                "ecdh-sha2-nistp256",
                "diffie-hellman-group14-sha256",
                "diffie-hellman-group14-sha1"
        }
        local hostkeys = {
                "ssh-ed25519",
                "ecdsa-sha2-nistp256",
                "rsa-sha2-512",
                "rsa-sha2-256",
                "ssh-rsa"
        }
        local ciphers = {
                "aes128-ctr",
                "aes256-ctr"
        }
        local macs = {
                "hmac-sha2-256",
                "hmac-sha2-512"
        }
        local compression = {"none"}

        return string.char(SSH_MSG_KEXINIT)
                .. random_bytes(16)
                .. name_list(kex)
                .. name_list(hostkeys)
                .. name_list(ciphers)
                .. name_list(ciphers)
                .. name_list(macs)
                .. name_list(macs)
                .. name_list(compression)
                .. name_list(compression)
                .. ssh_string("")
                .. ssh_string("")
                .. string.char(0)
                .. bytes.pack_u32be(0)
end

local function binary_packet(payload)
        local padding_length = 8 - ((#payload + 5) % 8)
        if padding_length < 4 then padding_length = padding_length + 8 end

        local packet_length = 1 + #payload + padding_length
        return bytes.pack_u32be(packet_length)
                .. string.char(padding_length)
                .. payload
                .. random_bytes(padding_length)
end

local function receive_packet(timeout_ms)
        local length_bytes, status = recv.exact(4, timeout_ms)
        if status ~= "ok" or not length_bytes or #length_bytes ~= 4 then
                return nil, "SSH packet length receive failed: " .. tostring(status)
        end

        local packet_length = bytes.u32be(length_bytes, 1)
        if packet_length < 6 or packet_length > PACKET_LIMIT then
                return nil, "SSH packet length outside enumeration bounds"
        end

        local packet, packet_status = recv.exact(packet_length, timeout_ms)
        if packet_status ~= "ok" or not packet or #packet ~= packet_length then
                return nil, "SSH packet receive failed: " .. tostring(packet_status)
        end

        local padding_length = bytes.u8(packet, 1)
        if padding_length < 4 or padding_length >= packet_length then
                return nil, "SSH packet has invalid padding length"
        end

        local payload_length = packet_length - padding_length - 1
        if payload_length < 1 then
                return nil, "SSH packet has empty payload"
        end

        return bytes.slice(packet, 2, payload_length), "ok"
end

local function split_name_list(value)
        local result = {}
        if value == "" then return result end

        for item in string.gmatch(value, "([^,]+)") do
                result[#result + 1] = item
        end
        return result
end

local function parse_name_list(payload, offset)
        if offset + 3 > #payload then return nil, offset, "truncated name-list length" end
        local length = bytes.u32be(payload, offset)
        offset = offset + 4
        if length > PACKET_LIMIT or offset + length - 1 > #payload then
                return nil, offset, "truncated name-list"
        end

        local raw = length == 0 and "" or bytes.slice(payload, offset, length)
        return split_name_list(raw), offset + length, nil
end

local function parse_kexinit(payload)
        if #payload < 17 or bytes.u8(payload, 1) ~= SSH_MSG_KEXINIT then
                return nil, "expected SSH_MSG_KEXINIT"
        end

        local offset = 18
        local fields = {}
        local names = {
                "kex",
                "hostkeys",
                "ciphers_c2s",
                "ciphers_s2c",
                "macs_c2s",
                "macs_s2c",
                "compression_c2s",
                "compression_s2c",
                "languages_c2s",
                "languages_s2c"
        }

        for _, name in ipairs(names) do
                local value, next_offset, err = parse_name_list(payload, offset)
                if err then return nil, err end
                fields[name] = value
                offset = next_offset
        end

        if offset + 4 > #payload then return nil, "truncated KEXINIT trailer" end
        fields.first_kex_packet_follows = bytes.u8(payload, offset) ~= 0
        offset = offset + 1
        fields.reserved = bytes.u32be(payload, offset)

        return fields, nil
end

function ssh.kex(options)
        options = options or {}
        local port = options.port or SSH_PORT
        local timeout_ms = options.timeout_ms

        result.inquiry("Opening TCP/" .. tostring(port) .. " for SSH key-exchange enumeration.")
        local opened, open_status = transport.tcp(port, timeout_ms)
        if not opened or open_status ~= "ok" then
                result.complete(
                        "TCP/" .. tostring(port)
                        .. " connection failed: "
                        .. tostring(open_status)
                )
                return nil, "TCP connect failed: " .. tostring(open_status)
        end
        result.complete("TCP/" .. tostring(port) .. " connection established.")

        result.inquiry("Requesting SSH server identification.")
        local banner, banner_status = server_identification(timeout_ms)
        if not banner then
                result.complete(
                        "SSH server identification failed: "
                        .. tostring(banner_status)
                )
                return nil, banner_status
        end
        result.complete("SSH server identification received: " .. banner)

        local client_ident = "SSH-2.0-Kaminowaku_0.0.9\r\n"
        result.inquiry("Sending SSH client identification.")
        local sent, send_status = send_complete(client_ident, timeout_ms)
        if not sent then
                result.complete(
                        "SSH client identification send failed: "
                        .. tostring(send_status)
                )
                return nil, "SSH identification send failed: " .. tostring(send_status)
        end
        result.complete(
                "SSH client identification sent ("
                .. tostring(sent)
                .. " bytes)."
        )

        local kex_packet = binary_packet(client_kexinit_payload())
        result.inquiry("Sending SSH KEXINIT.")
        sent, send_status = send_complete(kex_packet, timeout_ms)
        if not sent then
                result.complete(
                        "SSH KEXINIT send failed: "
                        .. tostring(send_status)
                )
                return nil, "SSH KEXINIT send failed: " .. tostring(send_status)
        end
        result.complete(
                "SSH KEXINIT sent ("
                .. tostring(sent)
                .. " bytes)."
        )

        result.inquiry("Requesting SSH server KEXINIT.")
        local server_payload, packet_status = receive_packet(timeout_ms)
        if not server_payload then
                result.complete(
                        "SSH server KEXINIT receive failed: "
                        .. tostring(packet_status)
                )
                return nil, packet_status
        end
        result.complete(
                "SSH server KEXINIT received ("
                .. tostring(#server_payload)
                .. " bytes)."
        )

        local parsed, parse_error = parse_kexinit(server_payload)
        if not parsed then return nil, parse_error end

        parsed.banner = banner
        parsed.port = port
        return parsed, nil
end

return ssh
