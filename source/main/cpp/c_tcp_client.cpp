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
#    include "rcore/c_task.h"
#    include "rcore/c_timer.h"

#    include "rwifi/c_tcp.h"
#    include "rwifi/c_udp.h"
#    include "rwifi/c_wifi_mgr.h"
#    include "rwifi/c_tcp_client.h"

namespace ncore
{
    namespace ntcp
    {
        // ------------------------------------------------------------
        // Static helpers
        // ------------------------------------------------------------

        // Reads a u16 from the TCP stream in little-endian format.
        // Returns false if not enough data is available to read.
        static bool s_read_u16(tcp_client_t& c, u16* out)
        {
            if (c.m_config->m_sock_ops.m_available(c.m_socket) < 2)
                return false;

            u8 hdr[2];
            c.m_config->m_sock_ops.m_read(c.m_socket, hdr, 2);
            *out = (u16(hdr[1]) << 8) | hdr[0];
            return true;
        }

        static void s_enter_backoff(tcp_client_t& c)
        {
            c.m_last_state = c.m_state;
            c.m_state      = TCP_STATE_BACKOFF;

            u32 next = c.m_backoff_ms << 1;
            if (next > c.m_config->m_timing.m_backoff_max_ms)
                next = c.m_config->m_timing.m_backoff_max_ms;

            c.m_backoff_ms      = next;
            c.m_last_attempt_ms = c.m_config->m_time_ops.m_millis();
        }

        static void s_attempt_connect(tcp_client_t& c)
        {
            u32 now = c.m_config->m_time_ops.m_millis();
            if ((now - c.m_last_attempt_ms) < c.m_backoff_ms)
                return;

            c.m_last_attempt_ms  = now;
            c.m_connect_start_ms = now;
            c.m_last_state       = c.m_state;
            c.m_state            = TCP_STATE_CONNECTING;

            c.m_config->m_sock_ops.m_stop(c.m_socket);
            c.m_config->m_sock_ops.m_connect(c.m_socket, c.m_ip, c.m_port);
        }

        static void s_poll_connecting(tcp_client_t& c)
        {
            if (c.m_config->m_sock_ops.m_connected(c.m_socket))
            {
                c.m_last_state        = c.m_state;
                c.m_state             = TCP_STATE_CONNECTED;
                c.m_backoff_ms        = c.m_config->m_timing.m_backoff_initial_ms;
                c.m_tcp_recv_expected = 0;
                c.m_tcp_recv_offset   = 0;
                c.m_tcp_recv_buf      = 0;

                if (c.m_last_state == TCP_STATE_CONNECTING && c.m_user_callbacks.m_on_connected)
                    c.m_user_callbacks.m_on_connected(c.m_user);

                return;
            }

            if ((c.m_config->m_time_ops.m_millis() - c.m_connect_start_ms) > c.m_config->m_timing.m_connect_timeout_ms)
            {
                c.m_config->m_sock_ops.m_stop(c.m_socket);
                s_enter_backoff(c);
            }
        }

        static void s_poll_backoff(tcp_client_t& c)
        {
            if ((c.m_config->m_time_ops.m_millis() - c.m_last_attempt_ms) >= c.m_backoff_ms)
            {
                c.m_last_state = c.m_state;
                c.m_state      = TCP_STATE_DISCONNECTED;
            }
        }

        static void s_poll_connected(tcp_client_t& c)
        {
            if (!c.m_config->m_sock_ops.m_connected(c.m_socket))
            {
                if (c.m_tcp_recv_buf && c.m_config->m_recv_ops.m_abort)
                    c.m_config->m_recv_ops.m_abort(c.m_tcp_recv_ctx);

                // Notify the user of disconnection if we were previously connected
                if (c.m_user_callbacks.m_on_disconnected)
                    c.m_user_callbacks.m_on_disconnected(c.m_user);

                c.m_tcp_recv_buf = 0;
                c.m_config->m_sock_ops.m_stop(c.m_socket);
                s_enter_backoff(c);
                return;
            }

            // No protocol defined means not receiving any data, just maintain the connection for sending
            if (c.m_config->m_fmp_ops.m_process_hdr == nullptr)
                return;

            while (c.m_config->m_sock_ops.m_available(c.m_socket) > 0)
            {
                if (c.m_tcp_recv_expected == 0)
                {
                    if (c.m_config->m_sock_ops.m_available(c.m_socket) < c.m_tcp_recv_header_size)
                        return;

                    if (c.m_config->m_sock_ops.m_read(c.m_socket, c.m_tcp_recv_header, c.m_tcp_recv_header_size) < 0)
                    {
                        if (c.m_config->m_recv_ops.m_abort)
                            c.m_config->m_recv_ops.m_abort(c.m_tcp_recv_ctx);
                        c.m_tcp_recv_expected = 0;
                        return;
                    }

                    c.m_tcp_recv_expected = c.m_config->m_fmp_ops.m_process_hdr(, c.m_tcp_recv_header_size);

                    c.m_tcp_recv_offset       = 0;
                    c.m_tcp_recv_buf.m_buffer = c.m_config->m_recv_ops.m_acquire(c.m_tcp_recv_ctx, c.m_tcp_recv_header);
                    c.m_tcp_recv_buf.m_length = c.m_tcp_recv_expected;

                    if (!c.m_tcp_recv_buf.m_buffer)
                    {
                        if (c.m_config->m_recv_ops.m_abort)
                            c.m_config->m_recv_ops.m_abort(c.m_tcp_recv_ctx);

                        c.m_tcp_recv_expected = 0;
                        return;
                    }
                }

                const u16 remaining = c.m_tcp_recv_expected - c.m_tcp_recv_offset;
                const u16 avail     = (u16)c.m_config->m_sock_ops.m_available(c.m_socket);
                const u16 chunk     = remaining < avail ? remaining : avail;

                c.m_config->m_sock_ops.m_read(c.m_socket, c.m_tcp_recv_buf.m_buffer + c.m_tcp_recv_offset, chunk);
                c.m_tcp_recv_offset += chunk;

                if (c.m_tcp_recv_offset == c.m_tcp_recv_expected)
                {
                    c.m_config->m_recv_ops.m_commit(c.m_tcp_recv_ctx, c.m_tcp_recv_header, c.m_tcp_recv_buf);

                    c.m_tcp_recv_expected = 0;
                    c.m_tcp_recv_offset   = 0;
                    c.m_tcp_recv_buf      = tcp_buffer_t(nullptr, 0);
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

            config->m_fmp_ops.m_process_hdr = nullptr;
            config->m_recv_ops.m_acquire    = nullptr;
            config->m_recv_ops.m_commit     = nullptr;
            config->m_recv_ops.m_abort      = nullptr;

            void* socket = setup(&config->m_sock_ops);

            return socket;
        }

        // ------------------------------------------------------------
        // Public API
        // ------------------------------------------------------------
        void setup(tcp_client_t& c, const config_t* config, void* socket, u32 ip, u16 port, void* recv_ctx, u16 recv_header_size)
        {
            c.m_socket = socket;
            c.m_ip     = ip;
            c.m_port   = port;

            c.m_config = config;

            c.m_enabled          = false;
            c.m_last_state       = TCP_STATE_DISABLED;
            c.m_state            = TCP_STATE_DISABLED;
            c.m_last_attempt_ms  = 0;
            c.m_connect_start_ms = 0;
            c.m_backoff_ms       = config->m_timing.m_backoff_initial_ms;

            c.m_tcp_recv_ctx         = recv_ctx;
            c.m_tcp_recv_header_size = recv_header_size;
            c.m_tcp_recv_expected    = 0;
            c.m_tcp_recv_offset      = 0;
            c.m_tcp_recv_buf         = 0;
        }

        void set_user_callbacks(tcp_client_t& c, void* user, tcp_callbacks_t callbacks)
        {
            c.m_user           = user;
            c.m_user_callbacks = callbacks;
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

            if (c.m_tcp_recv_buf && c.m_config->m_recv_ops.m_abort)
                c.m_config->m_recv_ops.m_abort(c.m_tcp_recv_ctx);

            c.m_tcp_recv_buf = nullptr;

            if (c.m_state == TCP_STATE_CONNECTED)
            {
                if (c.m_user_callbacks.m_on_disconnected)
                    c.m_user_callbacks.m_on_disconnected(c.m_user);

                c.m_config->m_sock_ops.m_stop(c.m_socket);
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
        bool tick_tcp_client(nwifi::wifi_manager_t* wifi_mgr, tcp_client_t& c)
        {
            if (!c.m_enabled)
                return false;

            if (nwifi::is_connected(wifi))
            {
                // If Wi-Fi just recovered or is active, allow TCP to run
                if (tcp.m_state == ntcp::TCP_STATE_INACTIVE)
                {
                    ntcp::activate(tcp);  // Boot up the TCP connection engine
                }

                switch (c.m_state)
                {
                    case TCP_STATE_DISCONNECTED: s_attempt_connect(c); break;
                    case TCP_STATE_CONNECTING: s_poll_connecting(c); break;
                    case TCP_STATE_CONNECTED: s_poll_connected(c); break;
                    case TCP_STATE_BACKOFF: s_poll_backoff(c); break;
                }
            }
            else
            {
                // Wi-Fi is down, backing off, or connecting.
                // Immediately suspend and reset TCP client state to protect resources
                if (tcp.m_state != ntcp::TCP_STATE_INACTIVE)
                {
                    deactivate(tcp);
                }
            }
            return c.m_state == TCP_STATE_CONNECTED;
        }

        bool send(tcp_client_t& c, const void* data, u16 len)
        {
            if (c.m_state != TCP_STATE_CONNECTED)
                return false;
            c.m_config->m_sock_ops.m_write(c.m_socket, data, len);
            return true;
        }

    }  // namespace ntcp
}  // namespace ncore

#endif

namespace ncore
{
    namespace ntcp
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

    }  // namespace ntcp
}  // namespace ncore
