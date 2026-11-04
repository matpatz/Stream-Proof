# Stream Proof

Yes, the entire project is vibecoded. I know absolutely no C++

Stream Proof is a Windows companion app and Luau client script. The client sends player
positions and selected player information to the app over a WebSocket connection on your own
computer. The app draws that information in a separate, transparent overlay; it does not use
Roblox's `Drawing` library.

> **This project requires an executor.** Use it only where you have permission and in accordance
> with the game's and executor's rules. The overlay's Windows screen-capture exclusion is
> best-effort, not a guarantee: behavior varies with Windows and the capture method.

## Get the project

When the repository is available, open PowerShell and clone it:

```powershell
git clone https://github.com/matpatz/Stream-Proof.git
Set-Location .\Stream-Proof
```

## Build the app and client script

The app and Lua client are separate files. Build both from the repository root.

### Install the build tools

- **Git** — clone the repository and fetch native dependencies.
- **CMake 3.16 or newer.**
- **64-bit mingw-w64** — provides `g++.exe` and `gcc.exe`. The build script checks `PATH`, the
  WinLibs WinGet package, `C:\msys64\mingw64\bin`, and `C:\mingw64\bin`.
- **Rokit** — install it using the [Rokit instructions](https://github.com/rojo-rbx/rokit). The
  repository's `rokit.toml` pins the Darklua version used to bundle the client.
- **Node.js 22 or newer** is optional; it is only needed for the host smoke test.

After installing, open a new PowerShell window and check that `git`, `cmake`, `rokit`, and the
toolchain's `g++` are available. If `g++` is not on `PATH` or in one of the locations above, note
the full path to its `g++.exe`; you can pass it to the build script below.

### Build

Run these commands from the repository root:

```powershell
rokit install
.\scripts\fetch-deps.ps1
darklua process .\luau\init.lua .\dist\streaming-enabled.lua
.\scripts\build.ps1
```

What each command does:

1. `rokit install` installs the repository-pinned Darklua tool.
2. `fetch-deps.ps1` downloads Dear ImGui, GLFW, and nlohmann/json into `third_party\`.
3. Darklua bundles `luau\init.lua` and its required modules into one executor-ready script.
4. `build.ps1` configures and builds the Windows app in Release mode.

If the build script cannot find your compiler, pass its path (the script uses the directory
containing `g++.exe`):

```powershell
.\scripts\build.ps1 -Toolchain 'C:\path\to\mingw64\bin\g++.exe'
```

The outputs are:

| File | Purpose |
| --- | --- |
| `build\StreamingEnabled.exe` | Windows overlay app |
| `dist\streaming-enabled.lua` | Single-file client script to run in your executor |

Keep the two files from the same build together. For a release, distribute both files; users do
not need Git, CMake, mingw-w64, Rokit, or Node.js.

## Start the app and run the script

1. Start Roblox and join a game.
2. Run `build\StreamingEnabled.exe` (or the executable from your release folder). Keep it open.
3. Open `dist\streaming-enabled.lua` in a text editor.
4. Copy the entire file.
5. Open your executor's script editor, paste the file, and execute it while Roblox is running.
   Do not run it as a Roblox Studio `Script` or `LocalScript`; it expects the executor APIs
   described below.
6. Switch back to Roblox. When the client is connected and has players to display, the overlay
   shows a `LIVE` status and the configured markers.

Your executor must support `WebSocket.connect(url)` and return a socket with `:Send(message)` and
`:Close()` methods. The client also uses Roblox game services such as `Players`, `Workspace`, and
`HttpService`. If your executor uses a different WebSocket API, this client is not compatible
without adapting `luau\websocket.lua`.

The default WebSocket address is `ws://127.0.0.1:34891`. The app listens on loopback only, so the
executor and app must run on the same computer. No router or port-forwarding setup is required.

## Default display

The client configuration in `luau\config.lua` selects:

- Players within 5,000 studs whose characters are on screen.
- A colored box, name, health bar, and distance.
- Team as an additional information line when available.

The overlay follows the Roblox window's client area, is click-through by default, and stays behind
other applications. It hides when it cannot find the configured Roblox window and shows again when
it can attach. To see it, switch back to Roblox.

The app asks Windows to exclude the overlay from screen capture. The HUD's `capture: excluded`
means Windows accepted that request; it does not guarantee that all recording or capture software
will omit the overlay. Test the capture method you actually use.

## Controls

| Shortcut | Action |
| --- | --- |
| Ctrl+Alt+H | Show or hide the overlay |
| Ctrl+Alt+Q | Quit the app |

To stop the client script, call `getgenv().StreamingEnabledStop()` from the executor if it supports
`getgenv`. Closing the app also ends the connection.

## Configuration

### Change the port

The app and client must use the same port. For example, to use `34892`:

1. Change `url` in `luau\config.lua` to `ws://127.0.0.1:34892`.
2. Regenerate `dist\streaming-enabled.lua`:

   ```powershell
   darklua process .\luau\init.lua .\dist\streaming-enabled.lua
   ```

3. Start the app with that port:

   ```powershell
   .\build\StreamingEnabled.exe --port 34892
   ```

4. Run the regenerated Lua file in the executor.

### Change what is displayed

Edit `luau\config.lua` to change the streamed fields, distance limit, whether your own character
and NPCs with a `Humanoid` are included, and whether off-screen characters are filtered. After
editing, regenerate `dist\streaming-enabled.lua` with the Darklua command above and run that new
bundle. Editing the source config alone does not update an existing bundle.

To see the app's command-line options, run:

```powershell
.\build\StreamingEnabled.exe --help
```

## Troubleshooting

- **The executor reports a connection error:** Start the app first and check that its port matches
  the `url` in the Lua config. Confirm that the executor supports the WebSocket API and socket
  methods listed above.
- **The app exits or will not start:** Check `streaming-enabled.log` beside the executable. If the
  log says the port is already in use, close the other app using it or choose a different port.
- **The app is open but there are no markers:** Confirm the script ran without errors and the HUD
  shows a connected client. By default, your own character and off-screen characters are not
  displayed. The HUD appears when there is data to draw.
- **The overlay is misplaced or hidden:** Make sure the Roblox client window is open and not
  minimized. The app follows `RobloxPlayerBeta.exe` by default; use `--game <executable-name>` if
  your client process has a different name.
- **The overlay appears in a recording:** Capture exclusion is controlled by Windows and may not
  work with every capture method. Check the HUD's capture status and test your specific setup.

## Tests (optional)

Start `build\StreamingEnabled.exe`, then, from the repository root, send sample data to the app:

```powershell
node .\tests\smoke-client.mjs
```

To run the bundled client in a Lua VM test harness instead of an executor, start the app and run:

```powershell
Set-Location .\tests
npm install
node .\lua-client.cjs ..\dist\streaming-enabled.lua
```

## Project layout

| Path | Contents |
| --- | --- |
| `src\` | Windows app, overlay, and WebSocket host |
| `luau\` | Client source and default configuration |
| `scripts\fetch-deps.ps1` | Downloads native build dependencies |
| `scripts\build.ps1` | Builds the Windows app |
| `.darklua.json` / `rokit.toml` | Darklua bundler configuration and pinned version |
| `tests\` | Optional host smoke test and Lua VM harness |
