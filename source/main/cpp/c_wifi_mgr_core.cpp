#include "rwifi/c_wifi_mgr_core.h"

#include "ccore/c_memory.h"
#include "rcore/c_eeprom.h"
#include "rcore/c_timer.h"

namespace ncore
{
    namespace nnet
    {
        struct wifi_manager_core_t
        {
            wifi_manager_config_t m_config;
            wifi_platform_ops_t   m_platform;
            wifi_cache_t          m_cache;
            u8                    m_ip_address[4];
            u8                    m_mac_address[6];
            i32                   m_rssi;
            wifi_state_t          m_state;
            u64                   m_last_attempt_ms;
            u32                   m_backoff_ms;
            u32                   m_current_wait_ms;
            bool                  m_has_cached_ip;
            bool                  m_has_cached_mac;
            bool                  m_connect_requested;
            bool                  m_use_fast_connect;
            u32                   m_connection_generation;
        };

        static bool is_cache_valid(wifi_cache_t* cache)
        {
            const u32 crc = cache->m_crc;
            cache->m_crc  = 0;
            const bool valid = crc == neeprom::crc32((const byte*)cache, sizeof(wifi_cache_t));
            cache->m_crc = crc;
            return valid;
        }

        static void save_cache(wifi_manager_core_t* manager)
        {
            if (!manager->m_platform.m_save_cache)
                return;

            manager->m_cache.m_crc = 0;
            manager->m_cache.m_crc = neeprom::crc32((const byte*)&manager->m_cache, sizeof(wifi_cache_t));
            manager->m_platform.m_save_cache(manager->m_platform.m_context, &manager->m_cache);
        }

        static void schedule_backoff(wifi_manager_core_t* manager, u64 now_ms)
        {
            u32 next_backoff = (u32)(manager->m_backoff_ms * manager->m_config.backoff_multiplier);
            manager->m_backoff_ms = next_backoff > manager->m_config.max_backoff_ms ? manager->m_config.max_backoff_ms : next_backoff;

            i32 max_jitter = (i32)(manager->m_backoff_ms * manager->m_config.jitter_percentage);
            i32 jitter     = 0;
            if (max_jitter > 0 && manager->m_config.random)
                jitter = g_random_s32_range(manager->m_config.random, -max_jitter, max_jitter + 1);

            i32 total_wait = (i32)manager->m_backoff_ms + jitter;
            if (total_wait < (i32)manager->m_config.init_backoff_ms)
                total_wait = manager->m_config.init_backoff_ms;
            if (total_wait > (i32)manager->m_config.max_backoff_ms)
                total_wait = manager->m_config.max_backoff_ms;

            manager->m_current_wait_ms = (u32)total_wait;
            manager->m_last_attempt_ms = now_ms;
            manager->m_state           = WIFI_STATE_BACKOFF;
        }

        static void update_link_info(wifi_manager_core_t* manager)
        {
            if (!manager->m_platform.m_get_link_info)
                return;

            wifi_link_info_t info;
            if (!manager->m_platform.m_get_link_info(manager->m_platform.m_context, &info))
                return;

            manager->m_cache          = info.m_cache;
            manager->m_rssi           = info.m_rssi;
            manager->m_has_cached_ip  = true;
            manager->m_ip_address[0]  = manager->m_cache.ip_address & 0xFF;
            manager->m_ip_address[1]  = (manager->m_cache.ip_address >> 8) & 0xFF;
            manager->m_ip_address[2]  = (manager->m_cache.ip_address >> 16) & 0xFF;
            manager->m_ip_address[3]  = (manager->m_cache.ip_address >> 24) & 0xFF;
            save_cache(manager);
        }

        static void process_event(wifi_manager_core_t* manager, const wifi_event_t* event, u64 now_ms)
        {
            if (event->m_type == WIFI_EVENT_GOT_IP)
            {
                update_link_info(manager);
                manager->m_backoff_ms = manager->m_config.init_backoff_ms;
                manager->m_state      = WIFI_STATE_CONNECTED;
                manager->m_connection_generation++;
            }
            else if (event->m_type == WIFI_EVENT_DISCONNECTED && manager->m_state != WIFI_STATE_INACTIVE && manager->m_state != WIFI_STATE_BACKOFF)
            {
                if (manager->m_state == WIFI_STATE_CONNECTING && manager->m_use_fast_connect)
                {
                    manager->m_use_fast_connect = false;
                    manager->m_state            = WIFI_STATE_DISCONNECTED;
                }
                else if (manager->m_state == WIFI_STATE_CONNECTING || manager->m_state == WIFI_STATE_CONNECTED)
                {
                    schedule_backoff(manager, now_ms);
                }
                else
                {
                    manager->m_state = WIFI_STATE_DISCONNECTED;
                }
            }
        }

        wifi_manager_core_t* create_wifi_manager_core(const wifi_manager_config_t* config, const wifi_platform_ops_t* platform)
        {
            if (!config || !platform || !platform->m_init || !platform->m_start || !platform->m_stop || !platform->m_connect || !platform->m_poll_event)
                return nullptr;

            wifi_manager_core_t* manager = new wifi_manager_core_t();
            manager->m_config             = *config;
            manager->m_platform           = *platform;
            manager->m_state              = WIFI_STATE_INACTIVE;
            manager->m_backoff_ms         = config->init_backoff_ms;
            manager->m_current_wait_ms    = config->init_backoff_ms;
            manager->m_last_attempt_ms    = 0;
            manager->m_rssi               = 0;
            manager->m_has_cached_ip      = false;
            manager->m_has_cached_mac     = false;
            manager->m_connect_requested  = false;
            manager->m_use_fast_connect   = false;
            manager->m_connection_generation = 0;
            g_memzero(&manager->m_cache, sizeof(manager->m_cache));
            g_memzero(manager->m_ip_address, sizeof(manager->m_ip_address));
            g_memzero(manager->m_mac_address, sizeof(manager->m_mac_address));

            if (!manager->m_platform.m_init(manager->m_platform.m_context))
            {
                delete manager;
                return nullptr;
            }

            if (manager->m_platform.m_load_cache && manager->m_platform.m_load_cache(manager->m_platform.m_context, &manager->m_cache) && is_cache_valid(&manager->m_cache) && manager->m_cache.ip_address != 0)
                manager->m_use_fast_connect = true;
            else
                g_memzero(&manager->m_cache, sizeof(manager->m_cache));

            return manager;
        }

        void destroy_wifi_manager_core(wifi_manager_core_t* manager)
        {
            if (!manager)
                return;

            deactivate_wifi_manager_core(manager);
            if (manager->m_platform.m_shutdown)
                manager->m_platform.m_shutdown(manager->m_platform.m_context);
            delete manager;
        }

        void activate_wifi_manager_core(wifi_manager_core_t* manager)
        {
            if (!manager || manager->m_state != WIFI_STATE_INACTIVE)
                return;

            manager->m_connect_requested = true;
            manager->m_state             = WIFI_STATE_DISCONNECTED;
            if (!manager->m_platform.m_start(manager->m_platform.m_context))
                schedule_backoff(manager, ntimer::millis());
        }

        void deactivate_wifi_manager_core(wifi_manager_core_t* manager)
        {
            if (!manager)
                return;

            manager->m_connect_requested = false;
            manager->m_state             = WIFI_STATE_INACTIVE;
            if (manager->m_platform.m_disconnect)
                manager->m_platform.m_disconnect(manager->m_platform.m_context);
            manager->m_platform.m_stop(manager->m_platform.m_context);
        }

        void tick_wifi_manager_core(wifi_manager_core_t* manager)
        {
            if (!manager || !manager->m_connect_requested)
                return;

            const u64 now_ms = ntimer::millis();
            wifi_event_t event;
            while (manager->m_platform.m_poll_event(manager->m_platform.m_context, &event))
                process_event(manager, &event, now_ms);

            switch (manager->m_state)
            {
                case WIFI_STATE_DISCONNECTED:
                {
                    wifi_connect_request_t request;
                    request.m_ssid         = manager->m_config.ssid;
                    request.m_password     = manager->m_config.password;
                    request.m_fast_connect = manager->m_use_fast_connect && manager->m_cache.ip_address != 0;
                    request.m_cache        = manager->m_cache;
                    manager->m_state       = WIFI_STATE_CONNECTING;
                    manager->m_last_attempt_ms = now_ms;
                    if (!manager->m_platform.m_connect(manager->m_platform.m_context, &request))
                        schedule_backoff(manager, now_ms);
                    break;
                }

                case WIFI_STATE_CONNECTING:
                {
                    const u64 timeout_ms = manager->m_use_fast_connect ? 4000 : 30000;
                    if (now_ms - manager->m_last_attempt_ms > timeout_ms)
                    {
                        if (manager->m_use_fast_connect)
                        {
                            manager->m_use_fast_connect = false;
                            manager->m_state            = WIFI_STATE_DISCONNECTED;
                        }
                        else
                        {
                            schedule_backoff(manager, now_ms);
                        }
                    }
                    break;
                }

                case WIFI_STATE_BACKOFF:
                    if (now_ms - manager->m_last_attempt_ms >= manager->m_current_wait_ms)
                    {
                        manager->m_use_fast_connect = false;
                        manager->m_state            = WIFI_STATE_DISCONNECTED;
                    }
                    break;

                case WIFI_STATE_INACTIVE:
                case WIFI_STATE_CONNECTED:
                default: break;
            }
        }

        bool is_wifi_manager_core_connected(const wifi_manager_core_t* manager)
        {
            return manager && manager->m_state == WIFI_STATE_CONNECTED;
        }

        wifi_link_state_t get_wifi_manager_core_link_state(const wifi_manager_core_t* manager)
        {
            if (manager == nullptr)
                return {false, 0};

            return {manager->m_state == WIFI_STATE_CONNECTED, manager->m_connection_generation};
        }

        u8 const* get_wifi_manager_core_ip_address(wifi_manager_core_t* manager)
        {
            return manager ? manager->m_ip_address : nullptr;
        }

        u8 const* get_wifi_manager_core_mac_address(wifi_manager_core_t* manager)
        {
            if (!manager)
                return nullptr;

            if (!manager->m_has_cached_mac && manager->m_platform.m_get_mac_address && manager->m_platform.m_get_mac_address(manager->m_platform.m_context, manager->m_mac_address))
                manager->m_has_cached_mac = true;
            return manager->m_mac_address;
        }

        i32 get_wifi_manager_core_rssi(const wifi_manager_core_t* manager)
        {
            return manager ? manager->m_rssi : 0;
        }

    }  // namespace nnet
}  // namespace ncore
