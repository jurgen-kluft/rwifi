#include "rcore/c_ipaddress.h"
#include "rcore/c_state.h"
#include "rcore/c_str.h"
#include "rwifi/c_tcp.h"
#include "ccore/c_memory.h"

#ifdef TARGET_ARDUINO

#    include "Arduino.h"

// #    include "rwifi/c_ethernet.h"
#    include "WiFi.h"

#    include "WiFiServer.h"
#    include "WiFiClient.h"

namespace ncore
{
    struct state_tcp_t
    {
        ncore::s16  m_NumClients = 0;
        bool        m_Active[4];
        WiFiClient* m_WiFiClient[4];
    };

    namespace nnet
    {
        namespace ntcp
        {
            state_tcp_t gTcpState;

            void init_state(state_t* state)
            {
                gTcpState.m_NumClients = 0;
                for (s16 i = 0; i < 4; ++i)
                {
                    gTcpState.m_Active[i]     = false;
                    gTcpState.m_WiFiClient[i] = nullptr;
                }
                state->Tcp = &gTcpState;
            }

#    ifdef TARGET_ESP32
            client_t connect(state_tcp_t* state, IPAddress_t const& _ip, u16 _port, s32 timeout_ms)
            {
                client_t client = -1;
                for (i32 i = 0; i < 4; ++i)
                {
                    if (state->m_Active[i] == false)
                    {
                        client = i;
                        if (state->m_WiFiClient[i] == nullptr)
                        {
                            state->m_WiFiClient[i] = new WiFiClient();
                        }
                        break;
                    }
                }
                if (client == -1)
                    return client;

                IPAddress ip;
                IPAddress_t::to_arduino(ip, _ip);
                if (state->m_WiFiClient[client]->connect(ip, _port, timeout_ms) == 0)
                    return -1;
                state->m_Active[client] = true;
                state->m_NumClients++;
                return client;
            }

            bool disconnect(state_tcp_t* state, client_t& client)
            {
                if (state->m_WiFiClient[client] == nullptr || state->m_NumClients == 0)
                    return false;
                state->m_WiFiClient[client]->stop();
                state->m_Active[client] = false;
                state->m_NumClients--;
                client = -1;
                return true;
            }

            s32 write(state_tcp_t* state, client_t client, const u8* buf, s32 size)
            {
                if (client == -1 || !state->m_Active[client])
                    return 0;
                return (s32)state->m_WiFiClient[client]->write(buf, size);
            }

            bool connected(state_tcp_t* state, client_t client)
            {
                if (client == -1 || !state->m_Active[client])
                    return false;
                return state->m_WiFiClient[client]->connected();
            }

            s32 available(state_tcp_t* state, client_t client)
            {
                if (client == -1 || !state->m_Active[client])
                    return 0;
                return state->m_WiFiClient[client]->available();
            }

#    endif

            IPAddress_t remote_IP(state_tcp_t* state, client_t client)
            {
                if (client == -1 || !state->m_Active[client])
                    return IPAddress_t{};
                IPAddress   ip = state->m_WiFiClient[client]->remoteIP();
                IPAddress_t ret;
                IPAddress_t::from_arduino(ret, ip);
                return ret;
            }

            u16 remote_port(state_tcp_t* state, client_t client)
            {
                if (client == -1 || !state->m_Active[client])
                    return 0;
                return state->m_WiFiClient[client]->remotePort();
            }

            IPAddress_t local_IP(state_tcp_t* state, client_t client)
            {
                if (client == -1 || !state->m_Active[client])
                    return IPAddress_t{};

                IPAddress   ip = state->m_WiFiClient[client]->localIP();
                IPAddress_t ret;
                IPAddress_t::from_arduino(ret, ip);
                return ret;
            }

            u16 local_port(state_tcp_t* state, client_t client)
            {
                if (client == -1 || !state->m_Active[client])
                    return 0;
                return state->m_WiFiClient[client]->localPort();
            }

#    ifdef TARGET_ESP8266

            client_t connect(state_tcp_t* state, IPAddress_t const& _ip, u16 _port, s32 timeout_ms)
            {
                if (state->m_NumClients == 4)
                    return -1;

                client_t client = -1;
                for (i32 i = 0; i < 4; ++i)
                {
                    if (state->m_Active[i] == false)
                    {
                        client = i;
                        if (state->m_WiFiClient[i] == nullptr)
                        {
                            state->m_WiFiClient[i] = new WiFiClient();
                        }
                        break;
                    }
                }
                if (client == -1)
                    return client;

                state->m_WiFiClient[client]->setTimeout(timeout_ms);

                IPAddress ip;
                IPAddress_t::to_arduino(ip, _ip);
                if (state->m_WiFiClient[client]->connect(ip, _port) == 0)
                    return -1;
                state->m_Active[client] = true;
                state->m_NumClients++;
                return client;
            }

            bool disconnect(state_tcp_t* state, client_t& client)
            {
                if (client == -1 || !state->m_Active[client])
                    return false;
                state->m_WiFiClient[client]->stop();
                state->m_Active[client] = false;
                state->m_NumClients--;
                client = -1;
                return true;
            }

            s32 write(state_tcp_t* state, client_t client, const u8* buf, s32 size)
            {
                if (client == -1 || !state->m_Active[client])
                    return 0;
                return state->m_WiFiClient[client]->write(buf, size);
            }

            bool connected(state_tcp_t* state, client_t client)
            {
                if (client == -1 || !state->m_Active[client])
                    return false;
                if (state->m_WiFiClient[client]->connected() == 0)
                    return false;
                return true;
            }

            s32 available(state_tcp_t* state, client_t client)
            {
                if (client == -1 || !state->m_Active[client])
                    return 0;
                return state->m_WiFiClient[client]->available();
            }

#    endif

        }  // namespace ntcp
    }  // namespace nnet
}  // namespace ncore

#else

namespace ncore
{
    namespace nnet
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
    }  // namespace nnet
}  // namespace ncore

#endif
