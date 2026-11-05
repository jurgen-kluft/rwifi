#include "rwifi/c_tcp_client.h"

#include "cunittest/cunittest.h"

#include <string.h>

using namespace ncore;

namespace
{
    static u32 g_now_ms = 0;

    struct fake_socket_t
    {
        bool m_connected;
        u32  m_connect_count;
        u32  m_stop_count;
        u32  m_ip;
        u16  m_port;
        byte m_written[32];
        u32  m_written_length;
    };

    static u32 fake_millis() { return g_now_ms; }

    static bool fake_connect(void* socket, u32 ip, u16 port)
    {
        fake_socket_t* fake = (fake_socket_t*)socket;
        fake->m_connect_count++;
        fake->m_ip   = ip;
        fake->m_port = port;
        return true;
    }

    static bool fake_connected(void* socket) { return ((fake_socket_t*)socket)->m_connected; }
    static i32  fake_available(void*) { return 0; }
    static i32  fake_read(void*, void*, u32) { return 0; }

    static i32 fake_write(void* socket, const void* src, u32 length)
    {
        fake_socket_t* fake = (fake_socket_t*)socket;
        if (length > sizeof(fake->m_written))
            return -1;

        memcpy(fake->m_written, src, length);
        fake->m_written_length = length;
        return (i32)length;
    }

    static void fake_stop(void* socket) { ((fake_socket_t*)socket)->m_stop_count++; }

    static nnet::config_t make_config()
    {
        nnet::config_t config = {};
        config.m_time_ops.m_millis = fake_millis;
        config.m_sock_ops.m_connect = fake_connect;
        config.m_sock_ops.m_connected = fake_connected;
        config.m_sock_ops.m_available = fake_available;
        config.m_sock_ops.m_read = fake_read;
        config.m_sock_ops.m_write = fake_write;
        config.m_sock_ops.m_stop = fake_stop;
        config.m_timing.m_connect_timeout_ms = 10;
        config.m_timing.m_backoff_initial_ms = 5;
        config.m_timing.m_backoff_jitter_ms = 0;
        config.m_timing.m_backoff_max_ms = 20;
        return config;
    }

}

UNITTEST_SUITE_BEGIN(tcp_client)
{
    UNITTEST_FIXTURE(connections)
    {
        UNITTEST_FIXTURE_SETUP() { g_now_ms = 100; }
        UNITTEST_FIXTURE_TEARDOWN() {}

        UNITTEST_TEST(connects_sends_and_stops_when_wifi_is_lost)
        {
            fake_socket_t      socket = {};
            nnet::config_t     config = make_config();
            nnet::tcp_client_t client;
            const byte          message[] = {7, 8, 9};

            nnet::setup(client, &config, &socket);
            nnet::connect(client, 0x0102A8C0, 9000);

            CHECK_FALSE(nnet::tick_tcp_client({true, 1}, client));
            CHECK_EQUAL(client.m_state, nnet::TCP_STATE_CONNECTING);
            CHECK_EQUAL(socket.m_connect_count, 1);
            CHECK_EQUAL(socket.m_ip, 0x0102A8C0);
            CHECK_EQUAL(socket.m_port, 9000);

            socket.m_connected = true;
            CHECK_TRUE(nnet::tick_tcp_client({true, 1}, client));
            CHECK_TRUE(nnet::is_connected(client));
            CHECK_TRUE(nnet::send(client, message, sizeof(message)));
            CHECK_EQUAL(socket.m_written_length, sizeof(message));
            CHECK_EQUAL(socket.m_written[0], message[0]);
            CHECK_EQUAL(socket.m_written[1], message[1]);
            CHECK_EQUAL(socket.m_written[2], message[2]);

            CHECK_FALSE(nnet::tick_tcp_client({false, 1}, client));
            CHECK_EQUAL(client.m_state, nnet::TCP_STATE_DISCONNECTED);
            CHECK_FALSE(nnet::send(client, message, sizeof(message)));
        }

        UNITTEST_TEST(retries_after_a_connect_timeout)
        {
            fake_socket_t      socket = {};
            nnet::config_t     config = make_config();
            nnet::tcp_client_t client;

            nnet::setup(client, &config, &socket);
            nnet::connect(client, 0x0102A8C0, 9000);
            CHECK_FALSE(nnet::tick_tcp_client({true, 1}, client));
            CHECK_EQUAL(socket.m_connect_count, 1);

            g_now_ms = 111;
            CHECK_FALSE(nnet::tick_tcp_client({true, 1}, client));
            CHECK_EQUAL(client.m_state, nnet::TCP_STATE_BACKOFF);

            g_now_ms = 121;
            CHECK_FALSE(nnet::tick_tcp_client({true, 1}, client));
            CHECK_EQUAL(client.m_state, nnet::TCP_STATE_DISCONNECTED);

            g_now_ms = 122;
            CHECK_FALSE(nnet::tick_tcp_client({true, 1}, client));
            CHECK_EQUAL(socket.m_connect_count, 2);
            CHECK_EQUAL(client.m_state, nnet::TCP_STATE_CONNECTING);
        }
    }
}
UNITTEST_SUITE_END