#include "rcore/c_app.h"
#include "rcore/c_network.h"
#include "rcore/c_eeprom.h"
#include "rcore/c_log.h"
#include "rcore/c_state.h"
#include "rcore/c_str.h"
#include "rcore/c_timer.h"

#include "rwifi/c_wifi_mgr.h"
#include "rwifi/c_tcp_client.h"

#ifdef TARGET_ARDUINO

#    include "Arduino.h"
#    include "WiFi.h"

namespace ncore
{
    namespace nnet
    {
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

    }  // namespace nnet
}  // namespace ncore

#endif