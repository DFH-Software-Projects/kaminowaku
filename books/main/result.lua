-- Copyright 2026 Jamison A. Drapeau
local result = {}

function result.emit(key, value, color)
        return _kami.emit(key, value, color)
end

function result.notice(text)
        return _kami.notice(text)
end

function result.inquiry(text)
        return _kami.inquiry(text)
end

function result.complete(text)
        return _kami.complete(text)
end

function result.bail(reason)
        return _kami.bail(reason)
end

return result
