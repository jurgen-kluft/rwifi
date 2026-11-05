#include "rwifi/c_udp_client.h"

#ifndef TARGET_ARDUINO

namespace ncore
{
    namespace nnet
    {
        static bool wifi_bind(void*, u16) { return false; }
        static i32  wifi_parse_packet(void*) { return 0; }
        static i32  wifi_read(void*, void*, u32) { return 0; }
        static u32  wifi_remote_ip(void*) { return 0; }
        static u16  wifi_remote_port(void*) { return 0; }
        static bool wifi_begin_packet(void*, u32, u16) { return false; }
        static i32  wifi_write(void*, const void*, u32) { return 0; }
        static bool wifi_end_packet(void*) { return false; }
        static void wifi_stop(void*) {}

        void* setup_default(udp_socket_ops_t* ops)
        {
            ops->m_bind         = wifi_bind;
            ops->m_parse_packet = wifi_parse_packet;
            ops->m_read         = wifi_read;
            ops->m_remote_ip    = wifi_remote_ip;
            ops->m_remote_port  = wifi_remote_port;
            ops->m_begin_packet = wifi_begin_packet;
            ops->m_write        = wifi_write;
            ops->m_end_packet   = wifi_end_packet;
            ops->m_stop         = wifi_stop;
            return nullptr;
        }

    }  // namespace nnet
}  // namespace ncore

#endif