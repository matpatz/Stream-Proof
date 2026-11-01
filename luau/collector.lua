local Players = game:GetService("Players")
local GuiService = game:GetService("GuiService")

local HALF_HEIGHT = 2.6
local collector = {}

local function attempt(call, ...)
    local ok, value = pcall(call, ...)
    if ok then
        return value
    end
    return nil
end

-- WorldToViewportPoint measures from the top of the 3D viewport, which starts
-- below the top bar. Without adding this inset every marker sits too high, and
-- it is most obvious on distant players because their box is tiny.
local inset_cache = { value = nil, at = 0 }

local function gui_inset()
    local now = os.clock()
    if inset_cache.value and now - inset_cache.at < 1 then
        return inset_cache.value
    end

    local value = attempt(function()
        return GuiService:GetGuiInset()
    end)
    inset_cache.value = typeof(value) == "Vector2" and value or Vector2.new(0, 0)
    inset_cache.at = now
    return inset_cache.value
end

function collector.screen_size()
    local camera = workspace.CurrentCamera
    if not camera then
        return 0, 0
    end

    -- Roblox's ViewportSize already equals the client area that
    -- WorldToScreenPoint reports in. Adding the GUI inset on top of it inflated
    -- the height (657 vs 599) and squashed every marker.
    local viewport = camera.ViewportSize
    return math.floor(viewport.X), math.floor(viewport.Y)
end

local function humanoid_of(player)
    local character = player.Character
    return character and character:FindFirstChildOfClass("Humanoid")
end

local function read_property(instance, name)
    local value = instance and attempt(function()
        return instance[name]
    end)
    local kind = typeof(value)
    if kind == "EnumItem" then
        return value.Name
    end
    if kind == "string" or kind == "number" or kind == "boolean" then
        return value
    end
    return nil
end

local READERS = {
    Name = function(player)
        return player.Name
    end,
    DisplayName = function(player)
        return player.DisplayName
    end,
    Health = function(player)
        local humanoid = humanoid_of(player)
        return humanoid and math.floor(humanoid.Health + 0.5)
    end,
    MaxHealth = function(player)
        local humanoid = humanoid_of(player)
        return humanoid and math.floor(humanoid.MaxHealth + 0.5)
    end,
    Team = function(player)
        local team = player.Team
        return team and team.Name
    end,
    Ping = function(player)
        local ping = attempt(function()
            return player:GetNetworkPing()
        end)
        return ping and math.floor(ping * 1000 + 0.5)
    end,
    UserId = function(player)
        return player.UserId
    end,
}

local function read_field(player, field, character)
    local reader = READERS[field]
    if reader then
        return attempt(reader, player)
    end
    return read_property(player, field) or read_property(character, field)
end

local function describe(player, camera, camera_position, config)
    local character = player.Character
    local root = character and character:FindFirstChild("HumanoidRootPart")
    if not root then
        return nil
    end

    local distance = (root.Position - camera_position).Magnitude
    if distance > config.max_distance then
        return nil
    end

    local item = {
        id = tostring(player.UserId),
        distance = math.floor(distance + 0.5),
    }

    local offset = Vector3.new(0, config.half_height or HALF_HEIGHT, 0)
    local head = camera:WorldToViewportPoint(root.Position + offset)
    local feet = camera:WorldToViewportPoint(root.Position - offset)

    if head.Z > 0 and feet.Z > 0 then
        local inset = config.apply_gui_inset == false and Vector2.new(0, 0) or gui_inset()
        local nudge = config.y_offset or 0
        local head_y = head.Y + inset.Y + nudge
        local feet_y = feet.Y + inset.Y + nudge
        local head_x = head.X + inset.X

        local height = math.abs(feet_y - head_y)
        local width = height * config.box_aspect
        item.x = math.floor(head_x - width * 0.5)
        item.y = math.floor(head_y)
        item.w = math.floor(width + 0.5)
        item.h = math.floor(height + 0.5)
    elseif config.on_screen_only then
        return nil
    end

    for _, field in ipairs(config.fields) do
        local value = read_field(player, field, character)
        if value ~= nil then
            if field == "Name" then
                item.name = tostring(value)
            elseif field == "Health" then
                item.health = tonumber(value)
                item.maxhealth = tonumber(read_field(player, "MaxHealth", character))
            else
                item[field] = value
            end
        end
    end

    if config.color_for_player then
        local color = attempt(config.color_for_player, player)
        if type(color) == "string" then
            item.color = color
        end
    end

    return item
end

function collector.snapshot(config)
    local camera = workspace.CurrentCamera
    if not camera then
        return {}
    end

    local camera_position = camera.CFrame.Position
    local local_player = Players.LocalPlayer
    local items = {}

    for _, player in ipairs(Players:GetPlayers()) do
        if config.include_local_player or player ~= local_player then
            local item = describe(player, camera, camera_position, config)
            if item then
                items[#items + 1] = item
            end
        end
    end

    return items
end

return collector
