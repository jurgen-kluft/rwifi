#ifndef __RWIFI_UDP_CLIENT_H__
#define __RWIFI_UDP_CLIENT_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "rwifi/c_tcp_client.h"

namespace ncore
{
    namespace nnet
    {
        struct udp_endpoint_t
        {
            u32 m_ip;
            u16 m_port;
        };

        typedef bool (*udp_bind_fn)(void* socket, u16 port);
        typedef i32 (*udp_parse_packet_fn)(void* socket);
        typedef i32 (*udp_read_fn)(void* socket, void* dst, u32 len);
        typedef u32 (*udp_remote_ip_fn)(void* socket);
        typedef u16 (*udp_remote_port_fn)(void* socket);
        typedef bool (*udp_begin_packet_fn)(void* socket, u32 ip, u16 port);
        typedef i32 (*udp_write_fn)(void* socket, const void* src, u32 len);
        typedef bool (*udp_end_packet_fn)(void* socket);
        typedef void (*udp_stop_fn)(void* socket);

        struct udp_socket_ops_t
        {
            udp_bind_fn         m_bind;
            udp_parse_packet_fn m_parse_packet;
            udp_read_fn         m_read;
            udp_remote_ip_fn    m_remote_ip;
            udp_remote_port_fn  m_remote_port;
            udp_begin_packet_fn m_begin_packet;
            udp_write_fn        m_write;
            udp_end_packet_fn   m_end_packet;
            udp_stop_fn         m_stop;
        };

        void* setup_default(udp_socket_ops_t* ops);

        struct udp_config_t
        {
            udp_socket_ops_t m_sock_ops;
        };

        void* setup_default(udp_config_t* config);

        struct udp_client_t
        {
            void*            m_socket;
            udp_socket_ops_t m_config_sock_ops;
            u16              m_port;
            bool             m_enabled;
            bool             m_bound;
            bool             m_wifi_connected;
            u32              m_wifi_connection_generation;
            udp_endpoint_t   m_remote_endpoint;

            tcp_recv_plugin_t* m_tcp_recv_plugins[8];
            msg_hdr_t          m_recv_header;
            byte               m_discard_buffer[64];
        };

        void               setup(udp_client_t& c, const udp_config_t* config, void* socket);
        void               register_plugin(udp_client_t& c, u8 id, tcp_recv_plugin_t* plugin);
        tcp_recv_plugin_t* get_plugin(udp_client_t& c, u8 id);
        tcp_recv_plugin_t* unregister_plugin(udp_client_t& c, u8 id);

        void bind(udp_client_t& c, u16 port);
        void unbind(udp_client_t& c);
        bool tick_udp_client(const wifi_link_state_t& wifi_link, udp_client_t& c);
        bool send_to(udp_client_t& c, const udp_endpoint_t& endpoint, const void* data, u32 len);

        inline bool is_bound(const udp_client_t& c) { return c.m_bound; }
        inline const udp_endpoint_t& get_remote_endpoint(const udp_client_t& c) { return c.m_remote_endpoint; }

    }  // namespace nnet
}  // namespace ncore

#endif  // __RWIFI_UDP_CLIENT_H__