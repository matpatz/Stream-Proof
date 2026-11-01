#pragma once

// Wire protocol between the Luau collector and this host.
//
// Transport: a single WebSocket on ws://127.0.0.1:<port> (loopback only).
// Every message is one JSON object in a text frame.
//
// Luau -> host
//   {"t":"hello","v":1,"executor":"...","game":"...","fields":["Name","Health"]}
//       Sent once per connection. `fields` is informational only.
//
//   {"t":"snapshot","seq":12,"items":[<entity>, ...]}
//       Full state of what should be drawn. Sent at the collector's tick rate.
//       A snapshot replaces the previous one; the host never accumulates state.
//
//   <entity> =
//       "id":       string   stable key, used to interpolate between snapshots
//       "name":     string   label drawn above the box (optional)
//       "x","y":    number   screen position of the box' top-left corner
//       "w","h":    number   box size in pixels; all four fields enable the box
//       "health":   number   current health (optional)
//       "maxhealth":number   maximum health (optional, defaults to 100)
//       "distance": number   distance shown under the box (optional)
//       "color":    string   "#RRGGBB" or "#RRGGBBAA" (optional)
//       anything else is drawn as a "key: value" line under the box.
//
//   {"t":"bye"}
//       Optional; the host keeps the last snapshot until the socket closes.
//
// Host -> Luau
//   {"t":"welcome","v":1,"server":"streaming-enabled"}
//       Sent immediately after the handshake completes.

namespace se {
namespace protocol {

constexpr int kVersion = 1;
constexpr const char* kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr uint16_t kDefaultPort = 34891;

}  // namespace protocol
}  // namespace se
