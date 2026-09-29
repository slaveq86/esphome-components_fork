# socket_transmitter

Small helper that sends a string to a remote host over TCP or UDP — typically to forward raw telegrams to a PC running wmbusmeters.

```yaml
socket_transmitter:
  id: my_socket
  ip_address: 192.168.1.10
  port: 3333
  protocol: TCP   # or UDP

wmbus_radio:
  on_frame:
    - then:
        - socket_transmitter.send:
            data: !lambda return frame->as_hex();
        # or the frame-aware shortcut registered by wmbus_radio:
        - wmbus_radio.send_frame_with_socket:
            format: rtlwmbus   # hex | raw | rtlwmbus
```

## Files
- `__init__.py` — schema (`MULTI_CONF`, `AUTO_LOAD = ["socket"]`), `SocketTransmitterSendAction`, `SOCKET_SEND_ACTION_SCHEMA`. `wmbus_radio` imports the last two to build `send_frame_with_socket` — keep those names stable.
- `socket_transmitter.{h,cpp}` — `SocketTransmitter` (host/port/protocol, `send`) and the templated send action (`play(const Ts&... x)`).

## Notes
- The action's `play` signature must match `Action<Ts...>::play` of the installed ESPHome. A mismatch only shows up when the action is used, so test with a send action in `on_frame`.
- `send()` opens a new socket per message; a TCP connect blocks the main loop until it succeeds or times out. Data is plain text, unauthenticated.
