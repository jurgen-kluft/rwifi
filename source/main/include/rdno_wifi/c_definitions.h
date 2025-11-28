#ifndef __RDNO_CORE_WIFI_DEFINITIONS_H__
#define __RDNO_CORE_WIFI_DEFINITIONS_H__
#include "rdno_core/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "rdno_core/c_network.h"

namespace ncore
{
    namespace nconfig
    {
        struct config_t;
    }

    namespace ntcp
    {
        typedef void* client_t;
    }

    namespace nwifi
    {
        // @see: https://www.arduino.cc/en/Reference/WiFi

        typedef s32 status_t;
        namespace nstatus
        {
            const status_t Idle            = 0;
            const status_t NoSSIDAvailable = 1;
            const status_t ScanCompleted   = 2;
            const status_t Connected       = 3;
            const status_t ConnectFailed   = 4;
            const status_t ConnectionLost  = 5;
            const status_t Disconnected    = 6;
            const status_t Stopped         = 254;
            const status_t NoShield        = 255;
        }  // namespace nstatus

        struct cache_t
        {
            u32 m_crc;
            u32 ip_address;
            u32 ip_gateway;
            u32 ip_mask;
            u32 ip_dns1;
            u32 ip_dns2;
            u16 wifi_channel;
            u8  wifi_bssid[6];
            void reset();
        };

        struct BSID_t
        {
            byte mB0, mB1, mB2, mB3, mB4, mB5, mB6, mB7;
        };
    }  // namespace nwifi

    struct state_wifi_t
    {
        u8   m_mac[6];
        nwifi::status_t m_status;
        nwifi::cache_t  m_cache;
    };
}  // namespace ncore

#endif  // __RDNO_CORE_WIFI_DEFINITIONS_H__
