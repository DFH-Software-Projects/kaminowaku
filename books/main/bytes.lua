-- Copyright 2026 Jamison A. Drapeau
local bytes = {}

-- Offsets are Lua-native and therefore 1-based.
local function bounds(data, offset, width)
        if type(data) ~= "string" then
                error("data must be a string", 3)
        end
        if type(offset) ~= "number" or offset % 1 ~= 0 or offset < 1 then
                error("offset must be a positive integer", 3)
        end
        if offset + width - 1 > #data then
                error("byte read exceeds input bounds", 3)
        end
end

local function uint(value, maximum)
        if type(value) ~= "number" or value % 1 ~= 0 or value < 0 or value > maximum then
                error("integer is outside encoding range", 3)
        end
        return value
end

function bytes.u8(data, offset)
        bounds(data, offset, 1)
        return string.byte(data, offset)
end

function bytes.u16be(data, offset)
        bounds(data, offset, 2)
        local a, b = string.byte(data, offset, offset + 1)
        return (a << 8) | b
end

function bytes.u16le(data, offset)
        bounds(data, offset, 2)
        local a, b = string.byte(data, offset, offset + 1)
        return a | (b << 8)
end

function bytes.u32be(data, offset)
        bounds(data, offset, 4)
        local a, b, c, d = string.byte(data, offset, offset + 3)
        return (a << 24) | (b << 16) | (c << 8) | d
end

function bytes.u32le(data, offset)
        bounds(data, offset, 4)
        local a, b, c, d = string.byte(data, offset, offset + 3)
        return a | (b << 8) | (c << 16) | (d << 24)
end

function bytes.pack_u8(value)
        value = uint(value, 0xff)
        return string.char(value)
end

function bytes.pack_u16be(value)
        value = uint(value, 0xffff)
        return string.char((value >> 8) & 0xff, value & 0xff)
end

function bytes.pack_u16le(value)
        value = uint(value, 0xffff)
        return string.char(value & 0xff, (value >> 8) & 0xff)
end

function bytes.pack_u32be(value)
        value = uint(value, 0xffffffff)
        return string.char(
                (value >> 24) & 0xff,
                (value >> 16) & 0xff,
                (value >> 8) & 0xff,
                value & 0xff
        )
end

function bytes.pack_u32le(value)
        value = uint(value, 0xffffffff)
        return string.char(
                value & 0xff,
                (value >> 8) & 0xff,
                (value >> 16) & 0xff,
                (value >> 24) & 0xff
        )
end

function bytes.slice(data, offset, length)
        length = uint(length, 0x7fffffff)
        if length == 0 then return "" end
        bounds(data, offset, length)
        return string.sub(data, offset, offset + length - 1)
end

function bytes.hex(data)
        if type(data) ~= "string" then
                error("data must be a string", 2)
        end
        return (string.gsub(data, ".", function(c)
                return string.format("%02x", string.byte(c))
        end))
end

function bytes.unhex(text)
        if type(text) ~= "string" then
                error("hex text must be a string", 2)
        end
        if #text % 2 ~= 0 or string.find(text, "[^0-9A-Fa-f]") then
                error("invalid hexadecimal text", 2)
        end

        local out = {}
        for i = 1, #text, 2 do
                out[#out + 1] = string.char(tonumber(string.sub(text, i, i + 1), 16))
        end
        return table.concat(out)
end

function bytes.concat(...)
        local values = {...}
        for i = 1, #values do
                if type(values[i]) ~= "string" then
                        error("bytes.concat accepts strings only", 2)
                end
        end
        return table.concat(values)
end

return bytes
