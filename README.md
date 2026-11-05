# rwifi

The main API this package provides is a TCP client that can be used to connect to a TCP server. The TCP client is designed to be used in an embedded environment, and as such, it is designed to be non-blocking and to use a small amount of memory.

## Setup
        void setup(tcp_client_t& c, const config_t* config, void* socket, u32 ip, u16 port);
        
## Plugins

You can create and register plugins to handle incoming data. A plugin handles a couple of things:

- acquire; this is called by the TCP loop when it has received the header of a message and wants to know which plugin will handle the message. The plugin can return true to indicate that it will handle the message, or false to indicate that it will not handle the message.
It also has to prepare a buffer to receive the message. 
- commit; this is called by the TCP loop when it has received the entire message and calls the plugin to commit the message.
- abort; This is called whenever the TCP loop identifies an error and wants to abort the message. The plugin can use this to clean up any resources it has allocated for the message.
- on complete; This callback is registered by the user of the plugin, and is called by the plugin when it has completed processing a message.
        
## Connection Management

The user can call connect() to initiate a connection to the server, and disconnect() to close the connection. The user can also call is_connected() to check if the client is currently connected to the server.

## Tick

The user must call tick() in the main loop to allow the TCP client to process incoming data and manage the connection. The tick() function will return true if the client is connected, and false if it is not connected.

## Send Data

The TCP client provides two functions to send data to the server: send() and send_later(). The send() function will send the data immediately, while the send_later() function will schedule the data to be sent at a later time. The user can call tick() to allow the TCP client to process the scheduled data and send it to the server.

## Wi-Fi Disconnect Recovery

The Wi-Fi manager publishes a `wifi_link_state_t` snapshot through `get_link_state()`. It contains `m_connected` and a monotonically increasing `m_connection_generation`, which advances every time Wi-Fi obtains an IP address. Pass the snapshot to every TCP and UDP client tick:

```cpp
const nnet::wifi_link_state_t wifi_link = nnet::get_link_state(wifi_manager);
nnet::tick_tcp_client(wifi_link, tcp_client);
nnet::tick_udp_client(wifi_link, udp_client);
```

### TCP

Keep calling `tick_tcp_client(wifi_link, tcp_client)` while the TCP client is enabled. When `m_connected` becomes false, the client aborts any partial receive, stops its socket, and waits without attempting a new connection. When Wi-Fi reconnects with a new `m_connection_generation`, the client resets stale socket state and reconnects to its configured endpoint. Existing plugins and callbacks remain registered. `send()` returns `false` until the TCP client reaches the connected state again. Call `disconnect()` only when the application wants to disable the TCP client permanently.

### UDP

Call `bind(udp_client, udp_port)` once to enable the client, then keep calling `tick_udp_client(wifi_link, udp_client)`. When `m_connected` becomes false, the client stops its `WiFiUDP` socket and clears its bound state. When Wi-Fi reconnects with a new `m_connection_generation`, it automatically binds the configured port again before receiving or sending. `send_to()` returns `false` while the UDP client is unbound. Call `unbind()` only when the application wants to disable UDP permanently.

## UDP Client

The UDP client uses the Wi-Fi link snapshot passed to `tick_udp_client()` to manage the lifetime of its socket. Use `send_to()` to send a datagram to any IPv4 address and port. `get_remote_endpoint()` returns the sender of the most recently received datagram, which can be used to reply.

UDP datagrams use the existing `msg_hdr_t` protocol. A datagram must contain exactly one complete message: the header magic must match `cMSG_MAGIC`, and `PayloadSize` must match the remaining datagram bytes. The UDP client can register the existing TCP receive plugins. It reads the header first, lets a plugin acquire its destination buffer, then reads the payload directly into that buffer before calling the plugin commit callback.

UDP is best effort. It does not provide ordering, acknowledgement, retries, duplicate detection, or cross-datagram reassembly. Plugins that process independent messages can be used directly; multi-datagram downloads need an application-level reliability protocol.

