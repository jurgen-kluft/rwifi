#ifndef __RWIFI_WIFI_PLATFORM_H__
#define __RWIFI_WIFI_PLATFORM_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

#include "rwifi/c_wifi_mgr.h"

namespace ncore
{
    namespace nnet
    {
        enum wifi_event_type_t
        {
            WIFI_EVENT_NONE = 0,
            WIFI_EVENT_GOT_IP,
            WIFI_EVENT_DISCONNECTED
        };

        struct wifi_event_t
        {
            wifi_event_type_t m_type;
        };

        struct wifi_link_info_t
        {
            wifi_cache_t m_cache;
            i32          m_rssi;
        };

        struct wifi_connect_request_t
        {
            const char*  m_ssid;
            const char*  m_password;
            bool         m_fast_connect;
            wifi_cache_t m_cache;
        };

        typedef bool (*wifi_platform_init_fn)(void* context);
        typedef void (*wifi_platform_shutdown_fn)(void* context);
        typedef bool (*wifi_platform_start_fn)(void* context);
        typedef void (*wifi_platform_stop_fn)(void* context);
        typedef bool (*wifi_platform_connect_fn)(void* context, const wifi_connect_request_t* request);
        typedef void (*wifi_platform_disconnect_fn)(void* context);
        typedef bool (*wifi_platform_poll_event_fn)(void* context, wifi_event_t* event);
        typedef bool (*wifi_platform_get_link_info_fn)(void* context, wifi_link_info_t* info);
        typedef bool (*wifi_platform_get_mac_address_fn)(void* context, u8* mac_address);
        typedef bool (*wifi_platform_load_cache_fn)(void* context, wifi_cache_t* cache);
        typedef void (*wifi_platform_save_cache_fn)(void* context, const wifi_cache_t* cache);

        struct wifi_platform_ops_t
        {
            void*                         m_context;
            wifi_platform_init_fn          m_init;
            wifi_platform_shutdown_fn      m_shutdown;
            wifi_platform_start_fn         m_start;
            wifi_platform_stop_fn          m_stop;
            wifi_platform_connect_fn       m_connect;
            wifi_platform_disconnect_fn    m_disconnect;
            wifi_platform_poll_event_fn    m_poll_event;
            wifi_platform_get_link_info_fn m_get_link_info;
            wifi_platform_get_mac_address_fn m_get_mac_address;
            wifi_platform_load_cache_fn    m_load_cache;
            wifi_platform_save_cache_fn    m_save_cache;
        };

    }  // namespace nnet
}  // namespace ncore

#endif  // __RWIFI_WIFI_PLATFORM_H__
