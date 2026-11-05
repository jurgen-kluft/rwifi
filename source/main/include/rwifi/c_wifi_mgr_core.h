#ifndef __RWIFI_WIFI_MANAGER_CORE_H__
#define __RWIFI_WIFI_MANAGER_CORE_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "ccore/c_random.h"
#include "rwifi/c_wifi_platform.h"
#include "rwifi/c_wifi_mgr.h"

namespace ncore
{
    namespace nnet
    {
        struct wifi_manager_core_t;

        struct wifi_manager_config_t
        {
            const char* ssid;
            const char* password;
            u32         init_backoff_ms;
            u32         max_backoff_ms;
            f32         backoff_multiplier;
            f32         jitter_percentage;
            random_t*   random;
        };

        wifi_manager_core_t* create_wifi_manager_core(const wifi_manager_config_t* config, const wifi_platform_ops_t* platform);
        void                 destroy_wifi_manager_core(wifi_manager_core_t* manager);
        void                 activate_wifi_manager_core(wifi_manager_core_t* manager);
        void                 deactivate_wifi_manager_core(wifi_manager_core_t* manager);
        void                 tick_wifi_manager_core(wifi_manager_core_t* manager);

        bool      is_wifi_manager_core_connected(const wifi_manager_core_t* manager);
        wifi_link_state_t get_wifi_manager_core_link_state(const wifi_manager_core_t* manager);
        u8 const* get_wifi_manager_core_ip_address(wifi_manager_core_t* manager);
        u8 const* get_wifi_manager_core_mac_address(wifi_manager_core_t* manager);
        i32       get_wifi_manager_core_rssi(const wifi_manager_core_t* manager);

    }  // namespace nnet
}  // namespace ncore

#endif  // __RWIFI_WIFI_MANAGER_CORE_H__
