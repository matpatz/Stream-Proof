--[[
my agent obv wrote this, but im guessing we use it over HttpService because the packaged data is sent to C++ which doesnt have it
so thats why this exists
]]

-- Minimal JSON encoder: enough for the snapshot messages, no dependencies.
local json = {}

local ESCAPES = {
    ['"'] = '\\"',
    ["\\"] = "\\\\",
    ["\b"] = "\\b",
    ["\f"] = "\\f",
    ["\n"] = "\\n",
    ["\r"] = "\\r",
    ["\t"] = "\\t",
}

local function escape_string(value)
    return (value:gsub('[%c"\\]', function(char)
        local known = ESCAPES[char]
        if known then
            return known
        end
        return string.format("\\u%04x", string.byte(char))
    end))
end

local function isvalid(value)
    if value == math.huge or value == -math.huge then
        return false
    end
end

local function encode_number(value)
    if value ~= value or value == math.huge or value == -math.huge then
        return "0"
    end
    if value % 1 == 0 and math.abs(value) < 1e15 then
        return string.format("%d", value)
    end
    return string.format("%.4f", value)
end

local function is_array(value)
    local count = 0
    for key in pairs(value) do
        if type(key) ~= "number" then
            return false
        end
        count = count + 1
    end
    return count == #value and count > 0
end

local encode_value

local function encode_table(value)
    local parts = {}
    if is_array(value) then
        for index = 1, #value do
            parts[index] = encode_value(value[index])
        end
        return "[" .. table.concat(parts, ",") .. "]"
    end
    for key, item in pairs(value) do
        parts[#parts + 1] = '"' .. escape_string(tostring(key)) .. '":' .. encode_value(item)
    end
    return "{" .. table.concat(parts, ",") .. "}"
end

function encode_value(value)
    local kind = type(value)
    if kind == "nil" then
        return "null"
    elseif kind == "boolean" then
        return value and "true" or "false"
    elseif kind == "number" then
        return encode_number(value)
    elseif kind == "string" then
        return '"' .. escape_string(value) .. '"'
    elseif kind == "table" then
        return encode_table(value)
    end
    return "null"
end

function json.encode(value)
    return encode_value(value)
end

return json
