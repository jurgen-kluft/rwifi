#include "rwifi/c_udp_client.h"

namespace ncore
{
    namespace nnet
    {
        static void s_discard(udp_client_t& c, u32 remaining)
        {
            while (remaining > 0)
            {
                const u32 read_size = remaining < sizeof(c.m_discard_buffer) ? remaining : sizeof(c.m_discard_buffer);
                const i32 read       = c.m_config_sock_ops.m_read(c.m_socket, c.m_discard_buffer, read_size);
                if (read <= 0)
                    return;

                remaining -= (u32)read;
            }
        }

        static void s_abort(tcp_recv_plugin_t* plugin)
        {
            if (plugin != nullptr && plugin->m_abort != nullptr)
                plugin->m_abort(plugin);
        }

        static bool s_update_wifi_link(udp_client_t& c, const wifi_link_state_t& wifi_link)
        {
            if (!wifi_link.m_connected)
            {
                if (c.m_wifi_connected && c.m_bound)
                    c.m_config_sock_ops.m_stop(c.m_socket);

                c.m_bound          = false;
                c.m_wifi_connected = false;
                return false;
            }

            const bool link_changed = !c.m_wifi_connected || c.m_wifi_connection_generation != wifi_link.m_connection_generation;
            c.m_wifi_connected            = true;
            c.m_wifi_connection_generation = wifi_link.m_connection_generation;
            if (link_changed && c.m_bound)
            {
                c.m_config_sock_ops.m_stop(c.m_socket);
                c.m_bound = false;
            }

            return true;
        }

        static bool s_read_packet(udp_client_t& c)
        {
            const i32 packet_size = c.m_config_sock_ops.m_parse_packet(c.m_socket);
            if (packet_size <= 0)
                return false;

            c.m_remote_endpoint.m_ip   = c.m_config_sock_ops.m_remote_ip(c.m_socket);
            c.m_remote_endpoint.m_port = c.m_config_sock_ops.m_remote_port(c.m_socket);

            if (packet_size < (i32)sizeof(msg_hdr_t))
            {
                s_discard(c, (u32)packet_size);
                return true;
            }

            const i32 header_read = c.m_config_sock_ops.m_read(c.m_socket, &c.m_recv_header, sizeof(msg_hdr_t));
            if (header_read != (i32)sizeof(msg_hdr_t))
            {
                const u32 consumed = header_read > 0 ? (u32)header_read : 0;
                s_discard(c, (u32)packet_size - consumed);
                return true;
            }

            const u32 payload_size = c.m_recv_header.PayloadSize;
            if (c.m_recv_header.Magic != cMSG_MAGIC || (u32)packet_size != sizeof(msg_hdr_t) + payload_size)
            {
                s_discard(c, (u32)packet_size - sizeof(msg_hdr_t));
                return true;
            }

            tcp_recv_plugin_t* active_plugin = nullptr;
            buffer_t           buffer        = {nullptr, 0};
            for (u32 plugin_index = 0; plugin_index < 8; ++plugin_index)
            {
                tcp_recv_plugin_t* plugin = c.m_tcp_recv_plugins[plugin_index];
                if (plugin != nullptr && plugin->m_acquire != nullptr && plugin->m_acquire(plugin, &c.m_recv_header, &buffer))
                {
                    active_plugin = plugin;
                    break;
                }
            }

            if (active_plugin == nullptr || buffer.m_length < payload_size || (payload_size > 0 && buffer.m_buffer == nullptr))
            {
                s_abort(active_plugin);
                s_discard(c, payload_size);
                return true;
            }

            u32 received = 0;
            while (received < payload_size)
            {
                const i32 read = c.m_config_sock_ops.m_read(c.m_socket, buffer.m_buffer + received, payload_size - received);
                if (read <= 0)
                {
                    s_abort(active_plugin);
                    s_discard(c, payload_size - received);
                    return true;
                }

                received += (u32)read;
            }

            if (active_plugin->m_commit != nullptr)
            {
                buffer.m_length = payload_size;
                active_plugin->m_commit(active_plugin, &c.m_recv_header, buffer);
            }
            return true;
        }

        void* setup_default(udp_config_t* config)
        {
            return setup_default(&config->m_sock_ops);
        }

        void setup(udp_client_t& c, const udp_config_t* config, void* socket)
        {
            c.m_socket          = socket;
            c.m_config_sock_ops = config->m_sock_ops;
            c.m_port            = 0;
            c.m_enabled         = false;
            c.m_bound           = false;
            c.m_wifi_connected  = false;
            c.m_wifi_connection_generation = 0;
            c.m_remote_endpoint = {0, 0};

            for (u32 index = 0; index < 8; ++index)
                c.m_tcp_recv_plugins[index] = nullptr;
        }

        void register_plugin(udp_client_t& c, u8 id, tcp_recv_plugin_t* plugin)
        {
            if (id < 8)
                c.m_tcp_recv_plugins[id] = plugin;
        }

        tcp_recv_plugin_t* get_plugin(udp_client_t& c, u8 id)
        {
            return id < 8 ? c.m_tcp_recv_plugins[id] : nullptr;
        }

        tcp_recv_plugin_t* unregister_plugin(udp_client_t& c, u8 id)
        {
            if (id >= 8)
                return nullptr;

            tcp_recv_plugin_t* plugin = c.m_tcp_recv_plugins[id];
            c.m_tcp_recv_plugins[id]  = nullptr;
            return plugin;
        }

        void bind(udp_client_t& c, u16 port)
        {
            if (c.m_enabled)
                return;

            c.m_port    = port;
            c.m_enabled = true;
        }

        void unbind(udp_client_t& c)
        {
            if (c.m_bound)
                c.m_config_sock_ops.m_stop(c.m_socket);

            c.m_bound   = false;
            c.m_enabled = false;
        }

        bool tick_udp_client(const wifi_link_state_t& wifi_link, udp_client_t& c)
        {
            if (!c.m_enabled)
                return false;

            if (!s_update_wifi_link(c, wifi_link))
                return false;

            if (!c.m_bound)
            {
                c.m_bound = c.m_config_sock_ops.m_bind(c.m_socket, c.m_port);
                if (!c.m_bound)
                    return false;
            }

            for (u32 packet_index = 0; packet_index < 4; ++packet_index)
            {
                if (!s_read_packet(c))
                    break;
            }

            return true;
        }

        bool send_to(udp_client_t& c, const udp_endpoint_t& endpoint, const void* data, u32 len)
        {
            if (!c.m_bound || !c.m_config_sock_ops.m_begin_packet(c.m_socket, endpoint.m_ip, endpoint.m_port))
                return false;

            const i32  written     = c.m_config_sock_ops.m_write(c.m_socket, data, len);
            const bool packet_sent = c.m_config_sock_ops.m_end_packet(c.m_socket);
            return written == (i32)len && packet_sent;
        }

    }  // namespace nnet
}  // namespace ncore