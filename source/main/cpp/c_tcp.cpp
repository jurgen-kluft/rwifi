#include "rdno_core/c_state.h"
#include "rdno_core/c_str.h"
#include "rdno_core/c_malloc.h"
#include "rdno_wifi/c_tcp.h"
#include "ccore/c_memory.h"

#ifdef TARGET_ARDUINO

#    include "Arduino.h"

#    include "rdno_wifi/c_ethernet.h"
#    include "WiFi.h"

#    include "WiFiServer.h"
#    include "WiFiClient.h"

namespace ncore
{
    struct state_tcp_t
    {
        ncore::s16 m_NumClients = 0;
        WiFiClient m_WiFiClient;
    };

    namespace ntcp
    {
        state_tcp_t gTcpState;

        void init_state(state_t* state)
        {
            gTcpState.m_NumClients = 0;
            state->tcp             = &gTcpState;
        }

#    ifdef TARGET_ESP32
        client_t connect(state_tcp_t* state, IPAddress_t _ip, u16 _port, s32 timeout_ms)
        {
            if (state->m_NumClients == 1)
                return nullptr;
            if (state->m_WiFiClient.connect(_ip, _port, timeout_ms) == 0)
                return nullptr;
            state->m_NumClients++;
            return &state->m_WiFiClient;
        }

        bool disconnect(state_tcp_t* state, client_t& client)
        {
            if (client == nullptr || state->m_NumClients == 0)
                return false;
            state->m_WiFiClient.stop();
            state->m_NumClients--;
            client = nullptr;
            return true;
        }

        s32 write(state_tcp_t* state, client_t client, const u8* buf, s32 size)
        {
            if (client == nullptr)
                return 0;
            return (s32)state->m_WiFiClient.write(buf, size);
        }

        bool connected(state_tcp_t* state, client_t client)
        {
            if (client == nullptr || state->m_WiFiClient.connected() == 0)
                return false;
            return true;
        }

        s32 available(state_tcp_t* state, client_t client)
        {
            if (client == nullptr)
                return 0;
            return state->m_WiFiClient.available();
        }

#    endif

        IPAddress_t remote_IP(state_tcp_t* state, client_t client)
        {
            return state->m_WiFiClient.remoteIP();
        }

        u16 remote_port(state_tcp_t* state, client_t client) { return state->m_WiFiClient.remotePort(); }

        IPAddress_t local_IP(state_tcp_t* state, client_t client)
        {
            return state->m_WiFiClient.localIP();
        }

        u16 local_port(state_tcp_t* state, client_t client) { return state->m_WiFiClient.localPort(); }

#    ifdef TARGET_ESP8266

        client_t connect(state_tcp_t* state, IPAddress_t _ip, u16 _port, s32 timeout_ms)
        {
            if (state->m_NumClients == 1)
                return nullptr;
            state->m_WiFiClient.setTimeout(timeout_ms);
            if (state->m_WiFiClient.connect(_ip, _port) == 0)
                return nullptr;
            state->m_NumClients++;
            return &state->m_WiFiClient;
        }

        bool disconnect(state_tcp_t* state, client_t& client)
        {
            if (client == nullptr || state->m_NumClients == 0)
                return false;
            state->m_WiFiClient.stop();
            state->m_NumClients--;
            client = nullptr;
            return true;
        }

        s32 write(state_tcp_t* state, client_t client, const u8* buf, s32 size)
        {
            if (client == nullptr)
                return 0;
            return state->m_WiFiClient.write(buf, size);
        }

        bool connected(state_tcp_t* state, client_t client)
        {
            if (client == nullptr)
                return false;
            if (state->m_WiFiClient.connected() == 0)
                return false;
            return true;
        }

        s32 available(state_tcp_t* state, client_t client)
        {
            if (client == nullptr)
                return 0;
            return state->m_WiFiClient.available();
        }

#    endif

    }  // namespace ntcp
}  // namespace ncore

#else

namespace ncore
{
    namespace ntcp
    {
        client_t          connect(state_tcp_t* state, IPAddress_t ip, u16 port, s32 timeout_ms) { return 1; }
        uint_t            write(state_tcp_t* state, client_t client, const u8* buf, uint_t size) { return size; }
        s32               available(state_tcp_t* state, client_t client) { return 1; }
        void              stop(state_tcp_t* state, client_t client) {}
        nstatus::status_t connected(state_tcp_t* state, client_t client) { return nstatus::Connected; }
        IPAddress_t       remote_IP(state_tcp_t* state, client_t client) { return IPAddress_t{10, 0, 0, 43}; }
        u16               remote_port(state_tcp_t* state, client_t client) { return 4242; }
        IPAddress_t       local_IP(state_tcp_t* state, client_t client) { return IPAddress_t{10, 0, 0, 42}; }
        u16               local_port(state_tcp_t* state, client_t client) { return 4242; }

    }  // namespace ntcp
}  // namespace ncore

#endif
