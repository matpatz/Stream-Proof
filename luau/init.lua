local config = require("./config")
local websocket = require("./websocket")
local collector = require("./collector")

local HttpService = game:GetService("HttpService")

local KEEPALIVE = 0.5

local SNAPSHOT = '{"t":"snapshot","seq":%d,"vw":%d,"vh":%d,"items":%s}'

local function log(...)
    if config.verbose then
        print("[streaming-enabled]", ...)
    end
end

local function identify_executor()
    return identifyexecutor and identifyexecutor() or "unknown"
end

local function publish_stopper(stop)
    local ok, environment = pcall(getgenv)
    if ok and type(environment) == "table" then
        environment.StreamingEnabledStop = stop
    end
end

local function run()
    local socket, connect_error = websocket.connect(config.url)
    assert(socket, "could not connect to " .. config.url .. ": " .. tostring(connect_error))
    log("connected to", config.url)

    websocket.send(
        socket,
        HttpService:JSONEncode({
            t = "hello",
            v = 1,
            executor = identify_executor(),
            game = tostring(game.PlaceId),
            fields = config.fields,
        })
    )

    local interval = 1 / math.max(1, config.update_rate)
    local running = true
    local sequence = 0
    local last_items = nil
    local last_sent = -math.huge

    publish_stopper(function()
        running = false
    end)

    while running do
        local started = time()
        local ok, items = pcall(collector.snapshot, config)

        if not ok then
            log("collection failed:", items)
        else
            local encoded = HttpService:JSONEncode(items)
            if encoded ~= last_items or time() - last_sent >= KEEPALIVE then
                local viewport_width, viewport_height = collector.screen_size()
                local message = SNAPSHOT:format(sequence, viewport_width, viewport_height, encoded)

                local sent, send_error = pcall(websocket.send, socket, message)
                if not sent then
                    log("send failed:", send_error)
                    break
                end

                last_items = encoded
                last_sent = time()
                sequence = sequence + 1
            end
        end

        local elapsed = time() - started
        if elapsed < interval then
            task.wait(interval - elapsed)
        end
    end

    pcall(websocket.send, socket, '{"t":"bye"}')
    websocket.close(socket)
    log("disconnected")
end

local ok, problem = pcall(run)
if not ok then
    log("stopped:", problem)
end

return ok
