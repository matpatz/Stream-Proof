-- Everything you normally touch lives here.
return {
    -- Keep the port in sync with the app (--port).
    url = "ws://127.0.0.1:34891",

    -- Snapshots per second. 60 matches most monitors; drop it if the executor
    -- struggles with the WebSocket traffic.
    update_rate = 60,

    -- Which fields to stream. `Name` becomes the label above the box, `Health`
    -- becomes the bar, `Distance` is drawn under the box, and everything else
    -- is printed as "key: value" under it.
    --
    -- Built-in readers: Name, DisplayName, Health, MaxHealth, Distance, Team,
    -- Ping, UserId. Anything not built in is read straight off the player or
    -- their character, so `"Character"`-style properties work too.
    fields = {
        "Name",
        "Health",
        "Distance",
        "Team",
    },

    -- Anything further than this is never sent (saves bandwidth).
    max_distance = 5000,

    -- Stream your own character too.
    include_local_player = false,

    -- Include NPC models with a living Humanoid.
    include_bots = true,

    -- Only stream players whose character is on screen.
    on_screen_only = true,

    -- Box width as a fraction of the projected head-to-feet height.
    box_aspect = 0.55,

    -- How much of the character to project, in studs above and below the
    -- HumanoidRootPart. Boxes look too tall/low when this is off.
    half_height = 2.6,

    -- World-position -> screen conversion.
    -- WorldToViewportPoint already returns coordinates in the space used by the
    -- host. Adding the GUI inset shifts markers down, so leave this disabled.
    apply_gui_inset = false,

    -- Manual nudge, in pixels. Positive moves markers down.
    y_offset = -4,

    -- "#RRGGBB" (or "#RRGGBBAA") per player; return nil to use the host palette.
    color_for_player = function(player)
        return nil
    end,

    -- Print connection problems to the executor console.
    verbose = true,
}
