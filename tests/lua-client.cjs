// Runs the real luau/ bundle in a small Lua VM against a stubbed Roblox
// environment and a real WebSocket, so the client half can be tested without an
// executor.
//
//   cd tests
//   npm install wasmoon
//   node lua-client.cjs ../dist/streaming-enabled.lua ws://127.0.0.1:34891 40
const fs = require("fs");
const path = require("path");

const { LuaFactory } = require(path.join(__dirname, "node_modules", "wasmoon"));

const bundlePath = process.argv[2] || path.join(__dirname, "..", "dist", "streaming-enabled.lua");
const url = process.argv[3] || "ws://127.0.0.1:34891";
const steps = Number(process.argv[4] || 40);

const PRELUDE = `
local VectorMT = {}
VectorMT.__index = function(self, key)
    if key == "Magnitude" then
        return math.sqrt(self.X * self.X + self.Y * self.Y + self.Z * self.Z)
    end
    return nil
end
VectorMT.__sub = function(a, b)
    return setmetatable({ X = a.X - b.X, Y = a.Y - b.Y, Z = a.Z - b.Z }, VectorMT)
end
VectorMT.__add = function(a, b)
    return setmetatable({ X = a.X + b.X, Y = a.Y + b.Y, Z = a.Z + b.Z }, VectorMT)
end
local function newvec(x, y, z) return setmetatable({ X = x, Y = y, Z = z }, VectorMT) end
Vector3 = { new = newvec }
Vector2 = { new = function(x, y) return { X = x, Y = y } end }
typeof = function(value)
    if type(value) == "table" and value.X ~= nil and value.Y ~= nil then return "Vector2" end
    return type(value)
end

local frame = 0
local camera = {
    CFrame = { Position = newvec(0, 0, 0) },
    ViewportSize = { X = 1920, Y = 1080 },
    WorldToViewportPoint = function(_, position)
        return newvec(400 + frame, 300 - position.Y * 12, 10)
    end,
}

local function make_player(name, userId, health, x)
    local root = { Name = "HumanoidRootPart", Position = newvec(x, 5, 10) }
    local humanoid = { Health = health, MaxHealth = 100 }
    local character = {
        FindFirstChild = function(_, child)
            if child == "HumanoidRootPart" then return root end
            return nil
        end,
        FindFirstChildOfClass = function(_, class)
            if class == "Humanoid" then return humanoid end
            return nil
        end,
    }
    return {
        Name = name,
        DisplayName = name,
        UserId = userId,
        Character = character,
        Team = { Name = "Red" },
        GetNetworkPing = function() return 0.063 end,
    }
end

local players = { make_player("bubby", 1, 78, 3), make_player("guest", 2, 22, -4) }
local player_service = { LocalPlayer = nil, GetPlayers = function() return players end }

local function encode_json(value)
    local kind = type(value)
    if kind == "nil" then return "null" end
    if kind == "boolean" or kind == "number" then return tostring(value) end
    if kind == "string" then
        return '"' .. value:gsub('[%c"\\\\]', function(char)
            return string.format("\\\\u%04x", string.byte(char))
        end) .. '"'
    end
    if kind ~= "table" then return "null" end

    local count, array = 0, true
    for key in pairs(value) do
        count = count + 1
        if type(key) ~= "number" then array = false end
    end
    if array and count == 0 then return "[]" end

    local parts = {}
    if array then
        for index = 1, count do parts[index] = encode_json(value[index]) end
        return "[" .. table.concat(parts, ",") .. "]"
    end
    for key, item in pairs(value) do
        parts[#parts + 1] = encode_json(tostring(key)) .. ":" .. encode_json(item)
    end
    return "{" .. table.concat(parts, ",") .. "}"
end

local http_service = { JSONEncode = function(_, value) return encode_json(value) end }

game = {
    PlaceId = 123456,
    GetService = function(_, name)
        if name == "Players" then return player_service end
        if name == "Workspace" then return { CurrentCamera = camera } end
        if name == "HttpService" then return http_service end
        if name == "GuiService" then
            return { GetGuiInset = function() return Vector2.new(0, 36) end }
        end
        return {}
    end,
}
workspace = { CurrentCamera = camera }
identifyexecutor = function() return "harness" end

local environment = {}
getgenv = function() return environment end

time = function() return frame end
task = { wait = function(_) coroutine.yield() end }

function __advance() frame = frame + 1 end
`;

(async () => {
  const socket = new WebSocket(url);
  await new Promise((resolve, reject) => {
    socket.onopen = resolve;
    socket.onerror = () => reject(new Error(`could not connect to ${url}`));
  });

  const received = [];
  socket.onmessage = (event) => received.push(String(event.data));

  const lua = await new LuaFactory().createEngine();
  lua.global.set("__connect", () => socket.readyState === 1);
  lua.global.set("__send", (text) => {
    if (socket.readyState === 1) socket.send(text);
  });
  lua.global.set("__close", () => socket.close());

  await lua.doString(`
    WebSocket = {
        connect = function(url)
            if not __connect(url) then return nil end
            return {
                Send = function(_, text) __send(text) end,
                Close = function() __close() end,
            }
        end,
    }
  `);
  await lua.doString(PRELUDE);
  lua.global.set("__bundle", fs.readFileSync(bundlePath, "utf8"));
  await lua.doString(`
    __chunk = assert(load(__bundle, "streaming-enabled"))
    __co = coroutine.create(__chunk)
    function __step()
        local ok, execution_error = coroutine.resume(__co)
        if not ok then error(execution_error) end
        return true
    end
  `);

  for (let i = 0; i < steps; ++i) {
    await lua.doString("__advance() __step()");
  }
  await lua.doString("local stop = getgenv().StreamingEnabledStop if stop then stop() end");
  for (let i = 0; i < 3; ++i) {
    try {
      await lua.doString("__step()");
    } catch (error) {
      break;
    }
  }

  console.log(`messages received by client: ${received.length}`);
  for (const message of received) console.log(`  <- ${message}`);
  if (socket.readyState === 1) socket.close();
  process.exit(received.length > 0 ? 0 : 1);
})().catch((error) => {
  console.error("harness failed:", error.message);
  process.exit(1);
});
