#include "rdno_core/c_str.h"
#include "rdno_core/c_state.h"
#include "rdno_wifi/c_udp.h"

#ifdef TARGET_ARDUINO

#    include "Arduino.h"

#    include "rdno_wifi/c_ethernet.h"
#    include "WiFi.h"

#    ifdef TARGET_ESP8266
#        include <ESP8266WiFi.h>
#        include <WiFiUdp.h>
#    endif

#    ifdef TARGET_ESP32
#        include "WiFiUdp.h"
#        include <lwip/sockets.h>
#        include <lwip/netdb.h>
#        include <errno.h>
#    endif

namespace ncore
{
#    ifdef TARGET_ESP32
    namespace nudp
    {
        struct udp_sock_t
        {
            int m_fd;
            u16 m_port;
            u16 m_flags;
        };
    }  // namespace nudp

    struct state_udp_t
    {
        nudp::udp_sock_t m_socks[2];

        void init()
        {
            for (s32 i = 0; i < DARRAYSIZE(m_socks); ++i)
            {
                m_socks[i].m_fd    = -1;
                m_socks[i].m_port  = 0xFFFF;
                m_socks[i].m_flags = 0;
            }
        }

        nudp::udp_sock_t *find_sock(u16 port)
        {
            for (s32 i = 0; i < DARRAYSIZE(m_socks); ++i)
            {
                if (m_socks[i].m_port == port)
                {
                    return &m_socks[i];
                }
            }
            return nullptr;
        }

        nudp::udp_sock_t *new_sock(u16 port)
        {
            for (s32 i = 0; i < DARRAYSIZE(m_socks); ++i)
            {
                if (m_socks[i].m_port == 0xFFFF)
                {
                    m_socks[i].m_port = port;
                    return &m_socks[i];
                }
            }
            return nullptr;
        }
    };

#    endif

#    ifdef TARGET_ESP8266
    namespace nudp
    {
        struct udp_sock_t
        {
            WiFiUDP m_udp;
            u16     m_port;
            u16     m_flags;
        };
    }  // namespace nudp
    struct state_udp_t
    {
        nudp::udp_sock_t m_socks[2];
        void             init()
        {
            for (s32 i = 0; i < DARRAYSIZE(m_socks); ++i)
            {
                m_socks[i].m_port  = 0xFFFF;
                m_socks[i].m_flags = 0;
            }
        }

        nudp::udp_sock_t *find_sock(u16 port)
        {
            for (s32 i = 0; i < DARRAYSIZE(m_socks); ++i)
            {
                if (m_socks[i].m_port == port)
                {
                    return &m_socks[i];
                }
            }
            return nullptr;
        }

        nudp::udp_sock_t *new_sock(u16 port)
        {
            for (s32 i = 0; i < DARRAYSIZE(m_socks); ++i)
            {
                if (m_socks[i].m_port == 0xFFFF)
                {
                    m_socks[i].m_port = port;
                    return &m_socks[i];
                }
            }
            return nullptr;
        }
    };
#    endif

    namespace nudp
    {
#    ifdef TARGET_ESP32
        state_udp_t gUdpState;

        void init_state(state_t *state)
        {
            gUdpState.init();
            state->udp = &gUdpState;
        }

        bool open(state_t *state, u16 port)
        {
            nudp::udp_sock_t *sock = state->udp->find_sock(port);
            if (sock != nullptr)
                return true;
            sock = state->udp->new_sock(port);
            if (sock == nullptr)
            {
                // No available socket
                return false;
            }

            IPAddress address(IPv4);
            if ((sock->m_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
            {
                log_e("could not create socket: %d", errno);
                sock->m_port = 0xFFFF;
                return false;
            }
            sock->m_port = port;

            int yes = 1;
            if (setsockopt(sock->m_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
            {
                log_e("could not set socket option: %d", errno);
                close(state, port);
                return false;
            }

            struct sockaddr_storage serveraddr = {};
            size_t                  sock_size  = 0;
            {
                struct sockaddr_in *tmpaddr = (struct sockaddr_in *)&serveraddr;
                memset((char *)tmpaddr, 0, sizeof(struct sockaddr_in));
                tmpaddr->sin_family      = AF_INET;
                tmpaddr->sin_port        = htons(sock->m_port);
                tmpaddr->sin_addr.s_addr = (in_addr_t)address;
                sock_size                = sizeof(sockaddr_in);
            }

            if (bind(sock->m_fd, (sockaddr *)&serveraddr, sock_size) == -1)
            {
                log_e("could not bind socket: %d", errno);
                close(state, port);
                return false;
            }
            fcntl(sock->m_fd, F_SETFL, O_NONBLOCK);

            sock->m_flags = 0;

            return true;
        }

        bool open_broadcast(state_t *state, u16 port)
        {
            nudp::udp_sock_t *sock = state->udp->new_sock(port);
            if (sock == nullptr)
            {
                // No available socket
                return false;
            }

            IPAddress address(IPv4);
            if ((sock->m_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
            {
                log_e("could not create socket: %d", errno);
                sock->m_port = 0xFFFF;
                return false;
            }
            sock->m_port = port;

            int yes = 1;
            if (setsockopt(sock->m_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
            {
                log_e("could not set socket option: %d", errno);
                close(state, port);
                return false;
            }

            // Enable broadcast
            if (setsockopt(sock->m_fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) < 0)
            {
                log_e("could not set broadcast option: %d", errno);
                close(state, port);
                return false;
            }

            struct sockaddr_storage serveraddr = {};
            size_t                  sock_size  = 0;
            {
                struct sockaddr_in *tmpaddr = (struct sockaddr_in *)&serveraddr;
                memset((char *)tmpaddr, 0, sizeof(struct sockaddr_in));
                tmpaddr->sin_family      = AF_INET;
                tmpaddr->sin_port        = htons(sock->m_port);
                tmpaddr->sin_addr.s_addr = (in_addr_t)address;
                sock_size                = sizeof(sockaddr_in);
            }

            if (bind(sock->m_fd, (sockaddr *)&serveraddr, sock_size) == -1)
            {
                log_e("could not bind socket: %d", errno);
                close(state, port);
                return false;
            }
            fcntl(sock->m_fd, F_SETFL, O_NONBLOCK);

            sock->m_flags |= 0x01;  // Mark as broadcast socket

            return true;
        }

        void close(state_t *state, u16 port)
        {
            nudp::udp_sock_t *sock = state->udp->find_sock(port);
            if (sock != nullptr && sock->m_port == port)
            {
                ::close(sock->m_fd);
                sock->m_port  = 0xFFFF;
                sock->m_fd    = -1;
                sock->m_flags = 0;
                sock          = nullptr;
            }
        }

        void close_broadcast(state_t *state, u16 port) { close(state, port); }

        s32 recv_from(state_t *state, u16 port, byte *rxdata, s32 max_rxdatasize, IPAddress_t &remote_ip, u16 &remote_port)
        {
            nudp::udp_sock_t *sock = state->udp->find_sock(port);
            if (sock == nullptr || sock->m_fd == -1)
            {
                return 0;
            }

            struct sockaddr_storage si_other_storage;  // enough storage for v4 and v6
            socklen_t               slen = sizeof(sockaddr_storage);
            int                     len  = 0;
            if (ioctl(sock->m_fd, FIONREAD, &len) == -1)
            {
                log_e("could not check for data in buffer length: %d", errno);
                return 0;
            }
            if (!len)
            {
                return 0;
            }
            if ((len = recvfrom(sock->m_fd, rxdata, len, MSG_DONTWAIT, (struct sockaddr *)&si_other_storage, (socklen_t *)&slen)) == -1)
            {
                if (errno == EWOULDBLOCK)
                {
                    return 0;
                }
                log_e("could not receive data: %d", errno);
                return 0;
            }
            if (si_other_storage.ss_family == AF_INET)
            {
                struct sockaddr_in &si_other = (sockaddr_in &)si_other_storage;
                remote_ip = IPAddress_t(si_other.sin_addr.s_addr);
                remote_port = ntohs(si_other.sin_port);
            }
            else
            {
                if (ip_addr_any.type  == ESP_IPADDR_TYPE_V4)
                {
                    remote_ip = IPAddress_t(ip_addr_any.u_addr.ip4.addr);
                }
                remote_port = 0;
            }
            return len;
        }

        s32 send_to(state_t *state, u16 port, byte const *data, s32 data_size, const IPAddress_t &to_address, u16 to_port)
        {
            nudp::udp_sock_t *sock = state->udp->find_sock(port);
            if (sock == nullptr || sock->m_fd == -1)
                return 0;

            byte const *tx_buffer     = data;
            u32 const   tx_buffer_len = data_size;

            IPAddress remote_ip = to_address;
            struct sockaddr_in recipient;
            recipient.sin_addr.s_addr = (uint32_t)remote_ip;
            recipient.sin_family      = AF_INET;
            recipient.sin_port        = htons(to_port);
            const int sent            = sendto(sock->m_fd, tx_buffer, tx_buffer_len, 0, (struct sockaddr *)&recipient, sizeof(recipient));
            if (sent > 0)
                return sent;

            log_e("could not send data: %d", errno);
            return 0;
        }
#    endif

#    ifdef TARGET_ESP8266
        udp_sock_t  gUdpSock;
        state_udp_t gUdpState;

        void init_state(state_t *state)
        {
            gUdpSock.m_port = 0xFFFF;
            state->udp      = &gUdpState;
        }

        bool open(state_t *state, u16 port)
        {
            udp_sock_t *sock = state->udp->find_sock(port);
            if (sock != nullptr)
                return true;
            sock = state->udp->new_sock(port);
            if (sock == nullptr)
            {
                // No available socket
                return false;
            }

            if (sock->m_udp.begin(port) == 0)
            {
                DEBUGV("could not open UDP socket");
                return false;
            }
            sock->m_port = port;
            return true;
        }

        void close(state_t *state, u16 port)
        {
            udp_sock_t *sock = state->udp->find_sock(port);
            if (sock != nullptr && sock->m_port != 0xFFFF)
            {
                sock->m_udp.stop();
                sock->m_port = 0xFFFF;
                sock         = nullptr;
            }
        }

        s32 recv_from(state_t *state, u16 port, byte *rxdata, s32 max_rxdatasize, IPAddress_t &remote_ip, u16 &remote_port)
        {
            udp_sock_t *sock = state->udp->find_sock(port);
            if (sock == nullptr || sock->m_port == 0xFFFF)
                return 0;

            int len = sock->m_udp.parsePacket();
            if (len <= 0)
            {
                return 0;
            }

            if (len > max_rxdatasize)
            {
                len = max_rxdatasize;
            }

            sock->m_udp.read(rxdata, len);

            remote_ip = sock->m_udp.remoteIP();
            remote_port = sock->m_udp.remotePort();

            return len;
        }

        s32 send_to(state_t *state, u16 port, byte const *data, s32 data_size, const IPAddress_t &to_address, u16 to_port)
        {
            udp_sock_t *sock = state->udp->find_sock(port);
            if (sock == nullptr || sock->m_port == 0xFFFF)
                return 0;

            sock->m_udp.beginPacket(to_address, to_port);
            sock->m_udp.write(data, data_size);
            sock->m_udp.endPacket();

            return data_size;
        }
#    endif

    }  // namespace nudp
}  // namespace ncore

#else

namespace ncore
{
    namespace nudp
    {
        bool open(sock_t& sock, u16 port)
        {
            CC_UNUSED(sock);
            CC_UNUSED(port);
            return false;
        }
        void close(sock_t& sock, u16 port)
        {
            CC_UNUSED(sock);
            CC_UNUSED(port);
        }

        s32 recv_from(sock_t& sock, u16 port, byte* data, s32 max_data_size, IPAddress_t& from_address)
        {
            CC_UNUSED(sock);
            CC_UNUSED(port);
            CC_UNUSED(data);
            CC_UNUSED(max_data_size);
            CC_UNUSED(from_address);
            return 0;
        }
        s32 send_to(sock_t& sock, u16 port, byte const* data, s32 data_size, IPAddress_t to_address, u16 to_port)
        {
            CC_UNUSED(sock);
            CC_UNUSED(port);
            CC_UNUSED(data);
            CC_UNUSED(data_size);
            CC_UNUSED(to_address);
            CC_UNUSED(to_port);
            return 0;
        }
    }  // namespace nudp

}  // namespace ncore

#endif
