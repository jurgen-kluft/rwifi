# rwifi

This library provides a 'node' for connecting to a WiFi network and Remote Server. It simplifies the process of connecting to a WiFi network by handling the connection logic and providing a simple interface for users.

It also uses non-volatile storage to load/save a configuration that can be setup by the user, it should provide the SSID and password, AP SSID and AP Password, remote server address and port, and any additional parameters that the application may need.

The boot process is as follows:
1. Node checks if there is a saved configuration in non-volatile storage otherwise a default configuration is used.
2. It attempts to connect to the specified WiFi network using the provided SSID and password, and subsequently tries to connect to the remote server and port.
3. If the connections are successful, it proceeds with normal operation.
4. If any connection fails (e.g., incorrect credentials, network not available), it starts a WiFi Access Point (AP) with the specified AP SSID and AP Password.
5. Users can connect to this AP using a smartphone or computer to send a TCP message in ASCII format to configure the WiFi credentials and other parameters. 
6. The configuration message should be a single line of text with key-value pairs separated by commas.

Example message format:
```
ssid=MyWiFiNetwork, password=MyPassword, ap_ssid=MyAP, ap_password=MyAPPassword, remote_server=10.0.0.42, remote_port=31337
```