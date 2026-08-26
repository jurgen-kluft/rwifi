#ifndef __RWIFI_TCP_CLIENT_H__
#define __RWIFI_TCP_CLIENT_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "rwifi/c_wifi_mgr.h"
#include "rwifi/c_protocol.h"

namespace ncore
{
    namespace nnet
    {
        enum tcp_state_t
        {
            TCP_STATE_INACTIVE     = 0,
            TCP_STATE_CONNECTING   = 1,
            TCP_STATE_BACKOFF      = 2,
            TCP_STATE_CONNECTED    = 3,
            TCP_STATE_DISCONNECTED = 4
        };

        // ------------------------------------------------------------
        // Function pointer typedefs
        // ------------------------------------------------------------

        typedef u32 (*millis_fn)();

        typedef bool (*tcp_connect_fn)(void* sock, u32 ip, u16 port);
        typedef bool (*tcp_connected_fn)(void* sock);
        typedef i32 (*tcp_available_fn)(void* sock);
        typedef i32 (*tcp_read_fn)(void* sock, void* dst, u32 len);
        typedef i32 (*tcp_write_fn)(void* sock, const void* src, u32 len);
        typedef void (*tcp_stop_fn)(void* sock);

        struct tcp_buffer_t
        {
            u8* m_buffer;
            u32 m_length;
        };

        struct tcp_recv_plugin_t;
        struct tcp_client_t;

        typedef bool (*tcp_recv_acquire_fn)(tcp_recv_plugin_t* plugin, msg_hdr_t* hdr, tcp_buffer_t* out);
        typedef void (*tcp_recv_commit_fn)(tcp_recv_plugin_t* plugin, msg_hdr_t* hdr, tcp_buffer_t buffer);
        typedef void (*tcp_recv_abort_fn)(tcp_recv_plugin_t* plugin);
        typedef void (*tcp_recv_complete_fn)(void* on_complete_context, u32 data_type, u32 data_size, byte const* data_ptr);

        // ------------------------------------------------------------
        // Ops groupings
        // ------------------------------------------------------------

        struct time_ops_t
        {
            millis_fn m_millis;
        };

        void setup(time_ops_t* ops);

        struct tcp_socket_ops_t
        {
            tcp_connect_fn   m_connect;
            tcp_connected_fn m_connected;
            tcp_available_fn m_available;
            tcp_read_fn      m_read;
            tcp_write_fn     m_write;
            tcp_stop_fn      m_stop;
        };

        void* setup(tcp_socket_ops_t* ops);

        struct tcp_recv_plugin_t
        {
            wifi_manager_t*      m_wifi_mgr;
            tcp_client_t*        m_client;
            void*                m_plugin_data;
            tcp_recv_acquire_fn  m_acquire;
            tcp_recv_commit_fn   m_commit;
            tcp_recv_abort_fn    m_abort;
            void*                m_on_complete_ctx;
            tcp_recv_complete_fn m_on_complete;
        };

        struct tcp_timing_t
        {
            u32 m_connect_timeout_ms;
            u32 m_backoff_initial_ms;
            u32 m_backoff_jitter_ms;
            u32 m_backoff_max_ms;
        };

        inline void setup(tcp_timing_t* timing)
        {
            // default timing values
            timing->m_connect_timeout_ms = 5000;       // 5 seconds
            timing->m_backoff_initial_ms = 1000;       // 1 second
            timing->m_backoff_jitter_ms  = 500;        // 0.5 seconds
            timing->m_backoff_max_ms     = 5 * 60000;  // 5 minutes
        }

        typedef void (*tcp_user_on_connected_fn)(void* ctx);
        typedef void (*tcp_user_on_disconnected_fn)(void* ctx);

        // ------------------------------------------------------------
        // TCP client
        // ------------------------------------------------------------
        struct config_t
        {
            time_ops_t       m_time_ops;
            tcp_socket_ops_t m_sock_ops;
            tcp_timing_t     m_timing;
        };
        void* setup_default(config_t* config);

        struct tcp_client_t
        {
            void* m_socket;
            u32   m_ip;
            u16   m_port;

            // config
            time_ops_t       m_config_time_ops;
            tcp_socket_ops_t m_config_sock_ops;
            tcp_timing_t     m_config_timing;

            // user
            void*                       m_on_connected_user;
            tcp_user_on_connected_fn    m_on_connected;
            void*                       m_on_disconnected_user;
            tcp_user_on_disconnected_fn m_on_disconnected;

            // runtime
            bool        m_enabled;
            tcp_state_t m_last_state;
            tcp_state_t m_state;
            u32         m_last_attempt_ms;
            u32         m_connect_start_ms;
            u32         m_backoff_ms;
            u32         m_backoff_jitter_ms;
            u32         m_backoff_attempts;

            // scheduled send
            byte m_scheduled_send_buffer[1024];
            u32  m_scheduled_send_length;

            // receiving, framing state
            tcp_recv_plugin_t* m_tcp_recv_plugins[8];  // Max 8 plugins
            void*              m_tcp_recv_plugin_ctx[8];
            tcp_recv_plugin_t* m_tcp_recv_active_plugin;
            u32                m_tcp_recv_expected;
            u32                m_tcp_recv_offset;
            u8                 m_tcp_recv_header[16];  // Must be at least sizeof(msg_hdr_t)
            tcp_buffer_t       m_tcp_recv_buf;
        };

        // ------------------------------------------------------------
        // API
        // ------------------------------------------------------------

        void setup(tcp_client_t& c, const config_t* config, void* socket, u32 ip, u16 port);
        void register_plugin(tcp_client_t& c, tcp_recv_plugin_t* plugin);
        void register_on_connected_callback(tcp_client_t& c, tcp_user_on_connected_fn on_connected, void* user_context);
        void register_on_disconnected_callback(tcp_client_t& c, tcp_user_on_disconnected_fn on_disconnected, void* user_context);

        void connect(tcp_client_t& c);
        void disconnect(tcp_client_t& c);
        bool tick_tcp_client(wifi_manager_t* wifi_mgr, tcp_client_t& c);
        bool send(tcp_client_t& c, const void* data, u32 len);
        bool send_later(tcp_client_t& c, const void* data, u32 len);

        inline bool is_connected(const tcp_client_t& c) { return c.m_state == TCP_STATE_CONNECTED; }

    }  // namespace nnet
}  // namespace ncore

#endif  // __RWIFI_TCP_CLIENT_H__
