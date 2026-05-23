#ifndef __RWIFI_WIFI_MANAGER_H__
#define __RWIFI_WIFI_MANAGER_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    namespace nwifi
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

        struct wifi_config_t
        {
            const char* ssid;
            const char* password;
            u32         init_backoff_ms;
            u32         max_backoff_ms;
            f32         backoff_multiplier;
            f32         jitter_percentage;  // e.g., 0.25f for +/- 25% variation
        };

        void init_wifi_config(wifi_config_t& config, const char* ssid, const char* password, u32 init_backoff_ms = 1000, u32 max_backoff_ms = 60000, f32 backoff_multiplier = 2.0f, f32 jitter_percentage = 0.25f);

        struct wifi_manager_t
        {
            // config
            const wifi_config_t* m_config;
            wifi_cache_t         m_cache;

            bool m_has_cached_mac;
            u8   m_mac_address[6];
            bool m_has_cached_ip;
            u8   m_ip_address[4];
            u32  m_rssi;

            // runtime
            wifi_state_t m_state;
            u32          m_last_attempt_ms;
            u32          m_backoff_ms;
            u32          m_current_wait_ms;  // Stores backoff_ms adjusted with jitter
            bool         m_connect_requested;
            bool         m_use_fast_connect;
        };

        void setup(wifi_manager_t& m, const wifi_config_t* config);
        void activate(wifi_manager_t& m);
        void deactivate(wifi_manager_t& m);
        void tick(wifi_manager_t& m);

        inline bool is_connected(const wifi_manager_t& m) { return m.m_state == WIFI_STATE_CONNECTED; }

        u8 const* get_ip_address(wifi_manager_t& m);
        u8 const* get_mac_address(wifi_manager_t& m);
        void      print_info(wifi_manager_t& m);

    }  // namespace nwifi
}  // namespace ncore

#endif  // __RWIFI_WIFI_MANAGER_H__
