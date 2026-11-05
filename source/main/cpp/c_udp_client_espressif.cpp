#include "rwifi/c_udp_client.h"

#ifdef TARGET_ARDUINO

#    include "Arduino.h"
#    include "WiFi.h"
#    include "WiFiUdp.h"

namespace ncore
{
    namespace nnet
    {
        static bool wifi_bind(void* socket, u16 port)
        {
            return ((WiFiUDP*)socket)->begin(port) != 0;
        }

        static i32 wifi_parse_packet(void* socket)
        {
            return (i32)((WiFiUDP*)socket)->parsePacket();
        }

        static i32 wifi_read(void* socket, void* dst, u32 len)
        {
            return (i32)((WiFiUDP*)socket)->read((u8*)dst, len);
        }

        static u32 wifi_remote_ip(void* socket)
        {
            return (u32)((WiFiUDP*)socket)->remoteIP();
        }

        static u16 wifi_remote_port(void* socket)
        {
            return (u16)((WiFiUDP*)socket)->remotePort();
        }

        static bool wifi_begin_packet(void* socket, u32 ip, u16 port)
        {
            return ((WiFiUDP*)socket)->beginPacket(IPAddress(ip), port) != 0;
        }

        static i32 wifi_write(void* socket, const void* src, u32 len)
        {
            return (i32)((WiFiUDP*)socket)->write((const u8*)src, len);
        }

        static bool wifi_end_packet(void* socket)
        {
            return ((WiFiUDP*)socket)->endPacket() != 0;
        }

        static void wifi_stop(void* socket)
        {
            ((WiFiUDP*)socket)->stop();
        }

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
            return new WiFiUDP();
        }

    }  // namespace nnet
}  // namespace ncore

#endif