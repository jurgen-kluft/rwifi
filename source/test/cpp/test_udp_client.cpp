#include "rwifi/c_udp_client.h"

#include "cunittest/cunittest.h"

#include <string.h>

using namespace ncore;

namespace
{
    struct fake_udp_t
    {
        byte                 m_packets[4][64];
        u32                  m_packet_sizes[4];
        u32                  m_packet_offsets[4];
        nnet::udp_endpoint_t m_packet_endpoints[4];
        u32                  m_packet_count;
        u32                  m_next_packet;
        u32                  m_active_packet;
        bool                 m_bound;
        u16                  m_bound_port;
        nnet::udp_endpoint_t m_send_endpoint;
        byte                 m_sent[64];
        u32                  m_sent_length;
        bool                 m_packet_open;
        bool                 m_packet_ended;
        u32                  m_stop_count;
    };

    struct fake_plugin_data_t
    {
        byte m_buffer[16];
        u32  m_length;
        u16  m_type;
        bool m_acquired;
        bool m_committed;
        bool m_aborted;
    };

    static bool fake_bind(void* socket, u16 port)
    {
        fake_udp_t* udp = (fake_udp_t*)socket;
        udp->m_bound     = true;
        udp->m_bound_port = port;
        return true;
    }

    static i32 fake_parse_packet(void* socket)
    {
        fake_udp_t* udp = (fake_udp_t*)socket;
        if (udp->m_next_packet == udp->m_packet_count)
            return 0;

        udp->m_active_packet = udp->m_next_packet++;
        return (i32)udp->m_packet_sizes[udp->m_active_packet];
    }

    static i32 fake_read(void* socket, void* dst, u32 len)
    {
        fake_udp_t* udp       = (fake_udp_t*)socket;
        const u32   packet    = udp->m_active_packet;
        const u32   remaining = udp->m_packet_sizes[packet] - udp->m_packet_offsets[packet];
        const u32   read_size = len < remaining ? len : remaining;
        memcpy(dst, udp->m_packets[packet] + udp->m_packet_offsets[packet], read_size);
        udp->m_packet_offsets[packet] += read_size;
        return (i32)read_size;
    }

    static u32 fake_remote_ip(void* socket)
    {
        fake_udp_t* udp = (fake_udp_t*)socket;
        return udp->m_packet_endpoints[udp->m_active_packet].m_ip;
    }

    static u16 fake_remote_port(void* socket)
    {
        fake_udp_t* udp = (fake_udp_t*)socket;
        return udp->m_packet_endpoints[udp->m_active_packet].m_port;
    }

    static bool fake_begin_packet(void* socket, u32 ip, u16 port)
    {
        fake_udp_t* udp       = (fake_udp_t*)socket;
        udp->m_send_endpoint  = {ip, port};
        udp->m_sent_length    = 0;
        udp->m_packet_open    = true;
        udp->m_packet_ended   = false;
        return true;
    }

    static i32 fake_write(void* socket, const void* src, u32 len)
    {
        fake_udp_t* udp = (fake_udp_t*)socket;
        if (!udp->m_packet_open || len > sizeof(udp->m_sent))
            return -1;

        memcpy(udp->m_sent, src, len);
        udp->m_sent_length = len;
        return (i32)len;
    }

    static bool fake_end_packet(void* socket)
    {
        fake_udp_t* udp    = (fake_udp_t*)socket;
        udp->m_packet_open = false;
        udp->m_packet_ended = true;
        return true;
    }

    static void fake_stop(void* socket)
    {
        fake_udp_t* udp = (fake_udp_t*)socket;
        udp->m_bound    = false;
        udp->m_stop_count++;
    }

    static nnet::udp_config_t make_config()
    {
        nnet::udp_config_t config         = {};
        config.m_sock_ops.m_bind           = fake_bind;
        config.m_sock_ops.m_parse_packet   = fake_parse_packet;
        config.m_sock_ops.m_read           = fake_read;
        config.m_sock_ops.m_remote_ip      = fake_remote_ip;
        config.m_sock_ops.m_remote_port    = fake_remote_port;
        config.m_sock_ops.m_begin_packet   = fake_begin_packet;
        config.m_sock_ops.m_write          = fake_write;
        config.m_sock_ops.m_end_packet     = fake_end_packet;
        config.m_sock_ops.m_stop           = fake_stop;
        return config;
    }

    static bool plugin_acquire(nnet::tcp_recv_plugin_t* plugin, nnet::msg_hdr_t* header, nnet::buffer_t* buffer)
    {
        fake_plugin_data_t* data = (fake_plugin_data_t*)plugin->m_plugin_data;
        if (header->Type != nnet::MSG_TYPE_HOUSE_SENSOR_DATA)
            return false;

        data->m_acquired = true;
        buffer->m_buffer = data->m_buffer;
        buffer->m_length = sizeof(data->m_buffer);
        return true;
    }

    static void plugin_commit(nnet::tcp_recv_plugin_t* plugin, nnet::msg_hdr_t* header, nnet::buffer_t buffer)
    {
        fake_plugin_data_t* data = (fake_plugin_data_t*)plugin->m_plugin_data;
        data->m_committed        = true;
        data->m_length           = buffer.m_length;
        data->m_type             = header->Type;
    }

    static void plugin_abort(nnet::tcp_recv_plugin_t* plugin)
    {
        ((fake_plugin_data_t*)plugin->m_plugin_data)->m_aborted = true;
    }

    static void queue_packet(fake_udp_t* udp, nnet::msg_hdr_t header, const byte* payload, u32 payload_size, nnet::udp_endpoint_t endpoint)
    {
        const u32 packet = udp->m_packet_count++;
        memcpy(udp->m_packets[packet], &header, sizeof(header));
        memcpy(udp->m_packets[packet] + sizeof(header), payload, payload_size);
        udp->m_packet_sizes[packet]     = sizeof(header) + payload_size;
        udp->m_packet_endpoints[packet] = endpoint;
    }
}

UNITTEST_SUITE_BEGIN(udp_client)
{
    UNITTEST_FIXTURE(datagrams)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(receives_directly_into_plugin_buffer)
        {
            fake_udp_t          socket = {};
            nnet::udp_config_t  config = make_config();
            nnet::udp_client_t  client;
            fake_plugin_data_t  plugin_data = {};
            nnet::tcp_recv_plugin_t plugin = {};
            plugin.m_plugin_data = &plugin_data;
            plugin.m_acquire     = plugin_acquire;
            plugin.m_commit      = plugin_commit;
            plugin.m_abort       = plugin_abort;

            const byte payload[] = {1, 2, 3};
            nnet::msg_hdr_t header = {};
            header.Magic       = nnet::cMSG_MAGIC;
            header.Type        = nnet::MSG_TYPE_HOUSE_SENSOR_DATA;
            header.PayloadSize = sizeof(payload);
            queue_packet(&socket, header, payload, sizeof(payload), {0x0102A8C0, 9000});

            nnet::setup(client, &config, &socket);
            nnet::register_plugin(client, 0, &plugin);
            nnet::bind(client, 7000);

            CHECK_TRUE(nnet::tick_udp_client({true, 1}, client));
            CHECK_TRUE(nnet::is_bound(client));
            CHECK_EQUAL(socket.m_bound_port, 7000);
            CHECK_TRUE(plugin_data.m_acquired);
            CHECK_TRUE(plugin_data.m_committed);
            CHECK_FALSE(plugin_data.m_aborted);
            CHECK_EQUAL(plugin_data.m_length, sizeof(payload));
            CHECK_EQUAL(plugin_data.m_buffer[0], payload[0]);
            CHECK_EQUAL(plugin_data.m_buffer[1], payload[1]);
            CHECK_EQUAL(plugin_data.m_buffer[2], payload[2]);
            CHECK_EQUAL(nnet::get_remote_endpoint(client).m_ip, 0x0102A8C0);
            CHECK_EQUAL(nnet::get_remote_endpoint(client).m_port, 9000);
        }

        UNITTEST_TEST(rejects_invalid_magic_without_blocking_next_packet)
        {
            fake_udp_t          socket = {};
            nnet::udp_config_t  config = make_config();
            nnet::udp_client_t  client;
            fake_plugin_data_t  plugin_data = {};
            nnet::tcp_recv_plugin_t plugin = {};
            plugin.m_plugin_data = &plugin_data;
            plugin.m_acquire     = plugin_acquire;
            plugin.m_commit      = plugin_commit;
            plugin.m_abort       = plugin_abort;

            const byte payload[] = {42};
            nnet::msg_hdr_t invalid = {};
            invalid.Magic       = 0;
            invalid.Type        = nnet::MSG_TYPE_HOUSE_SENSOR_DATA;
            invalid.PayloadSize = sizeof(payload);
            queue_packet(&socket, invalid, payload, sizeof(payload), {1, 2});

            nnet::msg_hdr_t valid = invalid;
            valid.Magic = nnet::cMSG_MAGIC;
            queue_packet(&socket, valid, payload, sizeof(payload), {3, 4});

            nnet::setup(client, &config, &socket);
            nnet::register_plugin(client, 0, &plugin);
            nnet::bind(client, 7000);

            CHECK_TRUE(nnet::tick_udp_client({true, 1}, client));
            CHECK_TRUE(plugin_data.m_committed);
            CHECK_EQUAL(plugin_data.m_buffer[0], payload[0]);
            CHECK_EQUAL(nnet::get_remote_endpoint(client).m_ip, 3);
            CHECK_EQUAL(nnet::get_remote_endpoint(client).m_port, 4);
        }

        UNITTEST_TEST(sends_to_requested_endpoint)
        {
            fake_udp_t         socket = {};
            nnet::udp_config_t config = make_config();
            nnet::udp_client_t client;
            const byte data[] = {9, 8, 7};

            nnet::setup(client, &config, &socket);
            nnet::bind(client, 7000);
            CHECK_TRUE(nnet::tick_udp_client({true, 1}, client));
            CHECK_TRUE(nnet::send_to(client, {0x0100A8C0, 9001}, data, sizeof(data)));
            CHECK_TRUE(socket.m_packet_ended);
            CHECK_EQUAL(socket.m_send_endpoint.m_ip, 0x0100A8C0);
            CHECK_EQUAL(socket.m_send_endpoint.m_port, 9001);
            CHECK_EQUAL(socket.m_sent_length, sizeof(data));
            CHECK_EQUAL(socket.m_sent[0], data[0]);
            CHECK_EQUAL(socket.m_sent[1], data[1]);
            CHECK_EQUAL(socket.m_sent[2], data[2]);
        }

        UNITTEST_TEST(rebinds_after_wifi_reconnect)
        {
            fake_udp_t         socket = {};
            nnet::udp_config_t config = make_config();
            nnet::udp_client_t client;

            nnet::setup(client, &config, &socket);
            nnet::bind(client, 7000);
            CHECK_TRUE(nnet::tick_udp_client({true, 1}, client));
            CHECK_TRUE(nnet::is_bound(client));

            CHECK_FALSE(nnet::tick_udp_client({false, 1}, client));
            CHECK_FALSE(nnet::is_bound(client));
            CHECK_EQUAL(socket.m_stop_count, 1);

            CHECK_TRUE(nnet::tick_udp_client({true, 2}, client));
            CHECK_TRUE(nnet::is_bound(client));
            CHECK_EQUAL(socket.m_bound_port, 7000);
        }
    }
}
UNITTEST_SUITE_END