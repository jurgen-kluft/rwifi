#    include "rcore/c_app.h"
#    include "rcore/c_network.h"
#    include "rcore/c_eeprom.h"
#    include "rcore/c_log.h"
#    include "rcore/c_state.h"
#    include "rcore/c_str.h"
#    include "rcore/c_timer.h"

#    include "rwifi/c_wifi_mgr.h"
#    include "rwifi/c_tcp_client.h"

namespace ncore
{
    namespace nnet
    {
        static u32 millis_cb() { return 0; }
        void       setup_default(time_ops_t* ops) { ops->m_millis = millis_cb; }

        static bool wifi_connect(void* socket, u32 ip, u16 port) { return false; }
        static bool wifi_connected(void* socket) { return false; }
        static int  wifi_available(void* socket) { return 0; }
        static int  wifi_read(void* socket, void* dst, u32 len) { return 0; }
        static int  wifi_write(void* socket, const void* src, u32 len) { return 0; }
        static void wifi_stop(void* sock) {}

        void* setup_default(tcp_socket_ops_t* ops)
        {
            ops->m_connect   = wifi_connect;
            ops->m_connected = wifi_connected;
            ops->m_available = wifi_available;
            ops->m_read      = wifi_read;
            ops->m_write     = wifi_write;
            ops->m_stop      = wifi_stop;

            return nullptr;
        }
    }  // namespace nnet
}  // namespace ncore
