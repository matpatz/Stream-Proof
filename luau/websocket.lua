-- Thin wrapper over the executor's WebSocket API. There is exactly one entry
-- point: `WebSocket.connect(url)`, returning an object with `:Send` and
-- `:Close`.
local websocket = {}

function websocket.connect(url)
    local ok, socket = pcall(WebSocket.connect, url)
    if not ok then
        return nil, socket
    end
    return socket
end

function websocket.send(socket, payload)
    socket:Send(payload)
end

function websocket.close(socket)
    pcall(socket.Close, socket)
end

return websocket
