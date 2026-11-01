#ifndef __RWIFI_WIFI_MANAGER_H__
#define __RWIFI_WIFI_MANAGER_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nnet
    {
        enum wifi_state_t
        {
            WIFI_STATE_DISCONNECTED = -1,
            WIFI_STATE_INACTIVE     = 0,
            WIFI_STATE_CONNECTING   = 1,
            WIFI_STATE_BACKOFF      = 2,
            WIFI_STATE_CONNECTED    = 3
        };

        struct wifi_cache_t
        {
            u32  ip_address;
            u32  ip_gateway;
            u32  ip_mask;
            u32  ip_dns1;
            u32  ip_dns2;
            u8   wifi_bssid[6];
            i32  wifi_channel;
            u32  m_crc;
            void reset()
            {
                ip_address = 0;
                ip_gateway = 0;
                ip_mask    = 0;
                ip_dns1    = 0;
                ip_dns2    = 0;
                for (int i = 0; i < 6; i++)
                    wifi_bssid[i] = 0;
                wifi_channel = 0;
                m_crc        = 0;
            }
        };

        struct wifi_manager_t;

        wifi_manager_t* create_wifi_mgr(const char* ssid, const char* password, u32 init_backoff_ms = 1000, u32 max_backoff_ms = 60000, f32 backoff_multiplier = 2.0f, f32 jitter_percentage = 0.25f);
        void            activate(wifi_manager_t* m);
        void            deactivate(wifi_manager_t* m);
        void            tick(wifi_manager_t* m);

        bool      is_connected(const wifi_manager_t* m);
        u8 const* get_ip_address(wifi_manager_t* m);
        u8 const* get_mac_address(wifi_manager_t* m);
        i32       get_rssi(wifi_manager_t* m);
        void      print_info(wifi_manager_t* m);

    }  // namespace nnet
}  // namespace ncore

#endif  // __RWIFI_WIFI_MANAGER_H__
