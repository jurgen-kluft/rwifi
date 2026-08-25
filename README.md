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

