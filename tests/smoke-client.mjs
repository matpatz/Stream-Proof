// Smoke test without Roblox: streams a few fake players at the host so you can
// see the overlay draw something. Node 22+ (built-in WebSocket), no deps.
//
//   node tests/smoke-client.mjs [port] [milliseconds]
const port = process.argv[2] || "34891";
const duration = Number(process.argv[3] || 5000);

const socket = new WebSocket(`ws://127.0.0.1:${port}`);

const frame = (sequence, elapsed) => {
  const bob = Math.sin(elapsed / 400) * 40;
  const sink = (elapsed / 25) % 200;
  return JSON.stringify({
    t: "snapshot",
    seq: sequence,
    items: [
      {
        id: "player-1",
        name: "bubby",
        x: 420,
        y: 260 + bob,
        w: 90,
        h: 180,
        health: 82,
        maxhealth: 100,
        distance: 42.5,
        color: "#5eb2ff",
        Team: "Red",
        Weapon: "Sword",
      },
      {
        id: "player-2",
        name: "guest",
        x: 760,
        y: 420 - sink,
        w: 70,
        h: 150,
        health: 21,
        maxhealth: 100,
        distance: 128,
        color: "#7ef09e",
        Ping: 63,
      },
      { id: "status", name: "player list", x: 1120, y: 220, Health: 100, Coins: 12345 },
    ],
  });
};

socket.onopen = () => {
  socket.send(JSON.stringify({ t: "hello", v: 1, executor: "smoke-client", game: "none" }));
  let sequence = 0;
  const started = Date.now();
  const timer = setInterval(() => socket.send(frame(++sequence, Date.now() - started)), 16);
  setTimeout(() => {
    clearInterval(timer);
    socket.close();
    process.exit(0);
  }, duration);
};

socket.onerror = () => {
  console.error(`could not reach ws://127.0.0.1:${port} - is the host running?`);
  process.exit(1);
};

socket.onmessage = (event) => console.log("<-", String(event.data));
