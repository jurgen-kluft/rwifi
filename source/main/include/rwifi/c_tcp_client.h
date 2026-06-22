#ifndef __ARDUINO_CORE_NODE_H__
#define __ARDUINO_CORE_NODE_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nwifi
    {
        struct wifi_manager_t;
    }

    namespace ntcp
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
        typedef int (*tcp_available_fn)(void* sock);
        typedef int (*tcp_read_fn)(void* sock, void* dst, u32 len);
        typedef int (*tcp_write_fn)(void* sock, const void* src, u32 len);
        typedef void (*tcp_stop_fn)(void* sock);

        struct tcp_buffer_t
        {
            u8* m_buffer;
            u16 m_length;
        };

        typedef tcp_buffer_t (*tcp_recv_acquire_fn)(void* ctx, void* hdr);
        typedef void (*tcp_recv_commit_fn)(void* ctx, void* hdr, tcp_buffer_t buffer);
        typedef void (*tcp_recv_abort_fn)(void* ctx);

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

        struct tcp_recv_ops_t
        {
            tcp_recv_acquire_fn m_acquire;
            tcp_recv_commit_fn  m_commit;
            tcp_recv_abort_fn   m_abort;
        };

        struct tcp_timing_t
        {
            u32 m_connect_timeout_ms;
            u32 m_backoff_initial_ms;
            u32 m_backoff_max_ms;
        };

        inline void setup(tcp_timing_t* timing)
        {
            // default timing values
            timing->m_connect_timeout_ms = 5000;       // 5 seconds
            timing->m_backoff_initial_ms = 1000;       // 1 second
            timing->m_backoff_max_ms     = 5 * 60000;  // 5 minutes
        }

        typedef void (*tcp_user_on_connected_fn)(void* ctx);
        typedef void (*tcp_user_on_disconnected_fn)(void* ctx);

        struct tcp_callbacks_t
        {
            tcp_user_on_connected_fn    m_on_connected;
            tcp_user_on_disconnected_fn m_on_disconnected;
        };

        // ------------------------------------------------------------
        // TCP client
        // ------------------------------------------------------------
        struct config_t
        {
            time_ops_t       m_time_ops;
            tcp_socket_ops_t m_sock_ops;
            tcp_recv_ops_t   m_recv_ops;
            tcp_timing_t     m_timing;
        };
        void* setup_default(config_t* config);

        struct tcp_client_t
        {
            void* m_socket;
            u32   m_ip;
            u16   m_port;

            // config
            const config_t* m_config;

            // user
            void*           m_user;
            tcp_callbacks_t m_user_callbacks;

            // runtime
            bool        m_enabled;
            tcp_state_t m_last_state;
            tcp_state_t m_state;
            u32         m_last_attempt_ms;
            u32         m_connect_start_ms;
            u32         m_backoff_ms;

            // receiving, framing state
            void*        m_tcp_recv_ctx;
            u16          m_tcp_recv_header_size;
            u16          m_tcp_recv_expected;
            u16          m_tcp_recv_offset;
            u8           m_tcp_recv_header[32];
            tcp_buffer_t m_tcp_recv_buf;
        };

        // ------------------------------------------------------------
        // API
        // ------------------------------------------------------------

        void setup(tcp_client_t& c, const config_t* config, void* socket, u32 ip, u16 port, void* recv_ctx = nullptr, u16 hdr_size = 0);
        void set_user_callbacks(tcp_client_t& c, void* user, tcp_callbacks_t callbacks);
        void connect(tcp_client_t& c);
        void disconnect(tcp_client_t& c);
        bool tick_tcp_client(nwifi::wifi_manager_t* wifi_mgr, tcp_client_t& c);
        bool send(tcp_client_t& c, const void* data, u16 len);

        inline bool is_connected(const tcp_client_t& c) { return c.m_state == TCP_STATE_CONNECTED; }

    }  // namespace ntcp
}  // namespace ncore

#endif  // __ARDUINO_CORE_NODE_H__
