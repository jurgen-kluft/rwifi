#ifndef __RWIFI_NETWORK_MANAGER_H__
#define __RWIFI_NETWORK_MANAGER_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "rwifi/c_wifi_mgr.h"
#include "rwifi/c_tcp_client.h"

namespace ncore
{
    namespace nnetwork
    {
        // ------------------------------------------------------------
        // Network Coordinator Integration, WiFi and TCP Client
        // ------------------------------------------------------------
        
        static inline void tick(nwifi::wifi_manager_t& wifi, ntcp::tcp_client_t& tcp)
        {
            // 1. Process the underlying Wi-Fi state machine first
            nwifi::tick(wifi);

            // 2. Drive the TCP client depending on the Wi-Fi status
            if (nwifi::is_connected(wifi))
            {
                // If Wi-Fi just recovered or is active, allow TCP to run
                if (tcp.m_state == ntcp::TCP_STATE_INACTIVE)
                {
                    ntcp::activate(tcp);  // Boot up the TCP connection engine
                }

                // Tick the TCP client state engine normally
                ntcp::tick(tcp);
            }
            else
            {
                // Wi-Fi is down, backing off, or connecting.
                // Immediately suspend and reset TCP client state to protect resources
                if (tcp.m_state != ntcp::TCP_STATE_INACTIVE)
                {
                    ntcp::deactivate(tcp);
                    tcp.m_state = ntcp::TCP_STATE_INACTIVE;
                    // ("NET_COORD", "Wi-Fi down. Forcing TCP client to inactive state.");
                }
            }
        }
    }  // namespace nnetwork
}  // namespace ncore

#endif  // __RWIFI_NETWORK_MANAGER_H__
