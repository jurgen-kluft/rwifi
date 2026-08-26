#ifdef TARGET_ARDUINO

#    include "Arduino.h"
#    include "WiFi.h"

#    include "rcore/c_app.h"
#    include "rcore/c_packet.h"
#    include "rcore/c_network.h"
#    include "rcore/c_eeprom.h"
#    include "rcore/c_log.h"
#    include "rcore/c_state.h"
#    include "rcore/c_str.h"
#    include "rcore/c_timer.h"

#    include "rwifi/c_tcp.h"
#    include "rwifi/c_udp.h"
#    include "rwifi/c_wifi_mgr.h"
#    include "rwifi/c_tcp_client.h"

namespace ncore
{
    namespace nnet
    {
        // ------------------------------------------------------------
        // Static helpers
        // ------------------------------------------------------------

        static void s_enter_backoff(tcp_client_t& c)
        {
            c.m_last_state = c.m_state;
            c.m_state      = TCP_STATE_BACKOFF;

            u32 next = c.m_backoff_ms << 1;
            if (next > c.m_config_timing.m_backoff_max_ms)
                next = c.m_config_timing.m_backoff_max_ms;

            c.m_backoff_ms = next;

            u32 jitter = c.m_config_timing.m_backoff_jitter_ms;
            if (jitter > 0)
            {
                const u32 rand_val = ((u32)ntimer::millis() & 0xFFF);
                c.m_backoff_ms     = c.m_backoff_ms - (jitter >> 1) + ((rand_val * jitter) >> 12);
            }
            else
            {
                c.m_backoff_jitter_ms = 0;
            }

            c.m_last_attempt_ms = c.m_config_time_ops.m_millis();
        }

        static void s_attempt_connect(tcp_client_t& c)
        {
            u32 now = c.m_config_time_ops.m_millis();
            if ((now - c.m_last_attempt_ms) < c.m_backoff_ms)
                return;

            c.m_last_attempt_ms  = now;
            c.m_connect_start_ms = now;
            c.m_last_state       = c.m_state;
            c.m_state            = TCP_STATE_CONNECTING;

            c.m_config_sock_ops.m_stop(c.m_socket);
            c.m_config_sock_ops.m_connect(c.m_socket, c.m_ip, c.m_port);
        }

        static void s_poll_connecting(tcp_client_t& c)
        {
            if (c.m_config_sock_ops.m_connected(c.m_socket))
            {
                c.m_last_state        = c.m_state;
                c.m_state             = TCP_STATE_CONNECTED;
                c.m_backoff_ms        = c.m_config_timing.m_backoff_initial_ms;
                c.m_tcp_recv_expected = 0;
                c.m_tcp_recv_offset   = 0;
                c.m_tcp_recv_buf      = {0, 0};

                if (c.m_last_state == TCP_STATE_CONNECTING && c.m_on_connected != nullptr)
                    c.m_on_connected(c.m_on_connected_user);

                return;
            }

            if ((c.m_config_time_ops.m_millis() - c.m_connect_start_ms) > c.m_config_timing.m_connect_timeout_ms)
            {
                c.m_config_sock_ops.m_stop(c.m_socket);
                s_enter_backoff(c);
            }
        }

        static void s_poll_backoff(tcp_client_t& c)
        {
            if ((c.m_config_time_ops.m_millis() - c.m_last_attempt_ms) >= c.m_backoff_ms)
            {
                c.m_last_state = c.m_state;
                if (c.m_backoff_attempts)
                    c.m_state = TCP_STATE_DISCONNECTED;
            }
        }

        static void s_poll_connected(tcp_client_t& c)
        {
            if (!c.m_config_sock_ops.m_connected(c.m_socket))
            {
                if (c.m_tcp_recv_buf.m_buffer && c.m_tcp_recv_active_plugin != nullptr)
                {
                    if (c.m_tcp_recv_active_plugin->m_abort != nullptr)
                        c.m_tcp_recv_active_plugin->m_abort(c.m_tcp_recv_active_plugin);
                    c.m_tcp_recv_active_plugin = nullptr;
                    c.m_tcp_recv_expected      = 0;
                    c.m_tcp_recv_offset        = 0;
                }

                // Notify the user of disconnection if we were previously connected
                if (c.m_on_disconnected)
                    c.m_on_disconnected(c.m_on_disconnected_user);

                c.m_tcp_recv_buf = {0, 0};
                c.m_config_sock_ops.m_stop(c.m_socket);
                s_enter_backoff(c);
                return;
            }

            while (c.m_config_sock_ops.m_available(c.m_socket) > 0)
            {
                msg_hdr_t* msg_hdr      = (msg_hdr_t*)c.m_tcp_recv_header;
                const i32  msg_hdr_size = (i32)sizeof(msg_hdr_t);

                if (c.m_tcp_recv_expected == 0)
                {
                    if (c.m_config_sock_ops.m_available(c.m_socket) < msg_hdr_size)
                        return;

                    if (c.m_config_sock_ops.m_read(c.m_socket, msg_hdr, msg_hdr_size) < 0)
                    {
                        if (c.m_tcp_recv_active_plugin != nullptr && c.m_tcp_recv_active_plugin->m_abort != nullptr)
                            c.m_tcp_recv_active_plugin->m_abort(c.m_tcp_recv_active_plugin);
                        c.m_tcp_recv_active_plugin = nullptr;
                        c.m_tcp_recv_expected      = 0;
                        c.m_tcp_recv_offset        = 0;
                        return;
                    }

                    // Plugin: Which plugin will handle this message?
                    // Request a buffer for the payload from the plugin.
                    // The last plugin in the array is one that always returns a buffer, so we
                    // will always have a buffer to receive the payload. However, that plugin
                    // is a "catch-all and ignore" plugin that will handle any message type that
                    // is not handled by the other plugins.
                    c.m_tcp_recv_offset = 0;
                    for (u32 plugin_index = 0; plugin_index < 8; ++plugin_index)
                    {
                        tcp_recv_plugin_t* plugin = c.m_tcp_recv_plugins[plugin_index];
                        if (plugin == nullptr)
                            continue;
                        if (plugin->m_acquire(plugin, msg_hdr, &c.m_tcp_recv_buf))
                        {
                            c.m_tcp_recv_active_plugin = plugin;
                            c.m_tcp_recv_expected      = msg_hdr->payload_len;
                            break;
                        }
                    }

                    if (!c.m_tcp_recv_buf.m_buffer)
                    {
                        if (c.m_tcp_recv_active_plugin != nullptr && c.m_tcp_recv_active_plugin->m_abort)
                        {
                            c.m_tcp_recv_active_plugin->m_abort(c.m_tcp_recv_active_plugin);
                            c.m_tcp_recv_active_plugin = nullptr;
                        }
                        c.m_tcp_recv_expected = 0;
                        return;
                    }
                }

                const u32 remaining = c.m_tcp_recv_expected - c.m_tcp_recv_offset;
                const u32 avail     = (u32)c.m_config_sock_ops.m_available(c.m_socket);
                const u32 chunk     = remaining < avail ? remaining : avail;

                c.m_config_sock_ops.m_read(c.m_socket, c.m_tcp_recv_buf.m_buffer + c.m_tcp_recv_offset, chunk);
                c.m_tcp_recv_offset += chunk;

                if (c.m_tcp_recv_offset == c.m_tcp_recv_expected)
                {
                    c.m_tcp_recv_active_plugin->m_commit(c.m_tcp_recv_active_plugin, msg_hdr, c.m_tcp_recv_buf);
                    c.m_tcp_recv_expected = 0;
                    c.m_tcp_recv_offset   = 0;
                    c.m_tcp_recv_buf      = tcp_buffer_t{nullptr, 0};
                }
            }
        }

        // ------------------------------------------------------------
        // Config
        // ------------------------------------------------------------

        void* setup_default(config_t* config)
        {
            setup(&config->m_time_ops);
            setup(&config->m_timing);
            void* socket = setup(&config->m_sock_ops);
            return socket;
        }

        // ------------------------------------------------------------
        // Public API
        // ------------------------------------------------------------
        void setup(tcp_client_t& c, const config_t* config, void* socket, u32 ip, u16 port)
        {
            c.m_socket = socket;
            c.m_ip     = ip;
            c.m_port   = port;

            c.m_config_time_ops = config->m_time_ops;
            c.m_config_sock_ops = config->m_sock_ops;
            c.m_config_timing   = config->m_timing;

            c.m_on_connected_user    = nullptr;
            c.m_on_connected         = nullptr;
            c.m_on_disconnected_user = nullptr;
            c.m_on_disconnected      = nullptr;

            c.m_enabled          = false;
            c.m_last_state       = TCP_STATE_INACTIVE;
            c.m_state            = TCP_STATE_INACTIVE;
            c.m_last_attempt_ms  = 0;
            c.m_connect_start_ms = 0;
            c.m_backoff_ms       = config->m_timing.m_backoff_initial_ms;

            for (u32 i = 0; i < 8; ++i)
            {
                c.m_tcp_recv_plugins[i]    = nullptr;
                c.m_tcp_recv_plugin_ctx[i] = nullptr;
            }

            c.m_tcp_recv_active_plugin = nullptr;
            c.m_tcp_recv_expected      = 0;
            c.m_tcp_recv_offset        = 0;
            c.m_tcp_recv_buf           = {nullptr, 0};
        }

        void register_plugin(tcp_client_t& c, tcp_recv_plugin_t* plugin)
        {
            for (u32 i = 0; i < 8; ++i)
            {
                if (c.m_tcp_recv_plugins[i] == nullptr)
                {
                    c.m_tcp_recv_plugins[i]    = plugin;
                    c.m_tcp_recv_plugin_ctx[i] = &c;
                    return;
                }
            }
        }

        void register_on_connected_callback(tcp_client_t& c, tcp_user_on_connected_fn on_connected, void* user_context)
        {
            c.m_on_connected_user = user_context;
            c.m_on_connected      = on_connected;
        }

        void register_on_disconnected_callback(tcp_client_t& c, tcp_user_on_disconnected_fn on_disconnected, void* user_context)
        {
            c.m_on_disconnected_user = user_context;
            c.m_on_disconnected      = on_disconnected;
        }

        static void activate(tcp_client_t& c)
        {
            if (!c.m_enabled || c.m_state != TCP_STATE_INACTIVE)
                return;
            c.m_last_state = c.m_state;
            c.m_state      = TCP_STATE_DISCONNECTED;
        }

        static void deactivate(tcp_client_t& c)
        {
            if (!c.m_enabled)
                return;

            if (c.m_tcp_recv_buf.m_buffer != nullptr && c.m_tcp_recv_active_plugin != nullptr)
            {
                if (c.m_tcp_recv_active_plugin->m_abort != nullptr)
                    c.m_tcp_recv_active_plugin->m_abort(c.m_tcp_recv_active_plugin);
                c.m_tcp_recv_active_plugin = nullptr;
                c.m_tcp_recv_expected      = 0;
                c.m_tcp_recv_offset        = 0;
            }

            c.m_tcp_recv_buf = {nullptr, 0};

            if (c.m_state == TCP_STATE_CONNECTED)
            {
                if (c.m_on_disconnected)
                    c.m_on_disconnected(c.m_on_disconnected_user);

                c.m_config_sock_ops.m_stop(c.m_socket);
            }

            c.m_last_state = c.m_state;
            c.m_state      = TCP_STATE_INACTIVE;
        }

        void connect(tcp_client_t& c)
        {
            c.m_enabled = true;
            activate(c);
        }

        void disconnect(tcp_client_t& c)
        {
            deactivate(c);
            c.m_enabled = false;
        }

        // returns true if the client is connected, false otherwise
        bool tick_tcp_client(nnet::wifi_manager_t* wifi_mgr, tcp_client_t& tcp)
        {
            if (!tcp.m_enabled)
                return false;

            switch (tcp.m_state)
            {
                case TCP_STATE_DISCONNECTED: s_attempt_connect(tcp); break;
                case TCP_STATE_CONNECTING: s_poll_connecting(tcp); break;
                case TCP_STATE_CONNECTED: s_poll_connected(tcp); break;
                case TCP_STATE_BACKOFF: s_poll_backoff(tcp); break;
            }

            return tcp.m_state == TCP_STATE_CONNECTED;
        }

        bool send(tcp_client_t& c, const void* data, u32 len)
        {
            if (c.m_state != TCP_STATE_CONNECTED)
                return false;
            c.m_config_sock_ops.m_write(c.m_socket, data, len);
            return true;
        }

        bool send_later(tcp_client_t& c, const void* data, u32 len)
        {
            // TODO implement a scheduled send mechanism that queues data to be
            // sent in the main loop instead of sending it directly from a callback context.
            // This will help avoid potential issues with sending data from interrupt or callback
            // contexts, which can lead to unexpected behavior or crashes.
            return false;
        }

    }  // namespace nnet
}  // namespace ncore

#endif

namespace ncore
{
    namespace nnet
    {
#ifdef TARGET_ARDUINO
        static u32 millis_cb() { return (u32)::millis(); }

        void setup(time_ops_t* ops) { ops->m_millis = millis_cb; }

        static bool wifi_connect(void* socket, u32 ip, u16 port)
        {
            IPAddress   ip_addr(ip);
            WiFiClient* wc = (WiFiClient*)socket;
            return wc->connect(ip_addr, port);
        }

        static bool wifi_connected(void* socket)
        {
            WiFiClient* wc = (WiFiClient*)socket;
            return wc->connected();
        }

        static int wifi_available(void* socket)
        {
            WiFiClient* wc = (WiFiClient*)socket;
            return wc->available();
        }

        static int wifi_read(void* socket, void* dst, u32 len)
        {
            WiFiClient* wc = (WiFiClient*)socket;
            return wc->read((u8*)dst, len);
        }

        static int wifi_write(void* socket, const void* src, u32 len)
        {
            WiFiClient* wc = (WiFiClient*)socket;
            return wc->write((const u8*)src, len);
        }

        static void wifi_stop(void* sock)
        {
            WiFiClient* wc = (WiFiClient*)sock;
            wc->stop();
        }

        void* setup(tcp_socket_ops_t* ops)
        {
            ops->m_connect   = wifi_connect;
            ops->m_connected = wifi_connected;
            ops->m_available = wifi_available;
            ops->m_read      = wifi_read;
            ops->m_write     = wifi_write;
            ops->m_stop      = wifi_stop;
            return new WiFiClient();
        }
#else

        static u32 millis_cb() { return 0; }
        void       setup(time_ops_t* ops) { ops->m_millis = millis_cb; }

        static bool wifi_connect(void* socket, const char* host, u16 port) { return false; }
        static bool wifi_connected(void* socket) { return false; }
        static int  wifi_available(void* socket) { return 0; }
        static int  wifi_read(void* socket, void* dst, u32 len) { return 0; }
        static int  wifi_write(void* socket, const void* src, u32 len) { return 0; }
        static void wifi_stop(void* sock) {}

        void* setup(tcp_socket_ops_t* ops)
        {
            ops->m_connect   = wifi_connect;
            ops->m_connected = wifi_connected;
            ops->m_available = wifi_available;
            ops->m_read      = wifi_read;
            ops->m_write     = wifi_write;
            ops->m_stop      = wifi_stop;

            return nullptr;
        }
#endif

        // // -------------------------------------------------------------------------------------------
        // // -------------------------------------------------------------------------------------------
        // // EXAMPLE
        // // TCP receiving, framing state (zero-copy)

        // static u8   g_tcp_recv_storage[2048];
        // static bool g_tcp_recv_in_use = false;

        // static u8* tcp_recv_acquire(void* ctx, u16 len)
        // {
        //     if (g_tcp_recv_in_use)
        //         return 0;

        //     if (len > sizeof(g_tcp_recv_storage))
        //         return 0;

        //     g_tcp_recv_in_use = true;
        //     return g_tcp_recv_storage;
        // }

        // static void tcp_recv_commit(void* ctx, u8* buffer, u16 len)
        // {
        //     // process message here
        //     g_tcp_recv_in_use = false;
        // }

        // static void tcp_recv_abort(void* ctx) { g_tcp_recv_in_use = false; }

        // -------------------------------------------------------------------------------------------
        // -------------------------------------------------------------------------------------------

    }  // namespace nnet
}  // namespace ncore
