#include "rwifi/c_wifi_mgr.h"

#ifdef TARGET_ESP32

#include "ccore/c_random.h"
#include "rwifi/c_wifi_mgr_core.h"
#include "rwifi/c_wifi_mgr_esp32.h"

namespace ncore
{
    namespace nnet
    {
        struct wifi_manager_t
        {
            xor_random_t          m_random;
            wifi_esp32_adapter_t* m_adapter;
            wifi_manager_core_t*  m_core;
        };

        wifi_manager_t* create_wifi_mgr(const char* ssid, const char* password, u32 init_backoff_ms, u32 max_backoff_ms, f32 backoff_multiplier, f32 jitter_percentage)
        {
            wifi_manager_t* manager = new wifi_manager_t();
            manager->m_adapter = create_wifi_esp32_adapter();
            if (!manager->m_adapter)
            {
                delete manager;
                return nullptr;
            }

            wifi_manager_config_t config = {};
            config.ssid = ssid;
            config.password = password;
            config.init_backoff_ms = init_backoff_ms;
            config.max_backoff_ms = max_backoff_ms;
            config.backoff_multiplier = backoff_multiplier;
            config.jitter_percentage = jitter_percentage;
            config.random = &manager->m_random;

            wifi_platform_ops_t platform = get_wifi_esp32_platform_ops(manager->m_adapter);
            manager->m_core = create_wifi_manager_core(&config, &platform);
            if (!manager->m_core)
            {
                destroy_wifi_esp32_adapter(manager->m_adapter);
                delete manager;
                return nullptr;
            }

            return manager;
        }

        void activate(wifi_manager_t* manager)
        {
            if (manager)
                activate_wifi_manager_core(manager->m_core);
        }

        void deactivate(wifi_manager_t* manager)
        {
            if (manager)
                deactivate_wifi_manager_core(manager->m_core);
        }

        void tick(wifi_manager_t* manager)
        {
            if (manager)
                tick_wifi_manager_core(manager->m_core);
        }

        bool is_connected(const wifi_manager_t* manager)
        {
            return manager && is_wifi_manager_core_connected(manager->m_core);
        }

        u8 const* get_ip_address(wifi_manager_t* manager)
        {
            return manager ? get_wifi_manager_core_ip_address(manager->m_core) : nullptr;
        }

        u8 const* get_mac_address(wifi_manager_t* manager)
        {
            return manager ? get_wifi_manager_core_mac_address(manager->m_core) : nullptr;
        }

        i32 get_rssi(wifi_manager_t* manager)
        {
            return manager ? get_wifi_manager_core_rssi(manager->m_core) : 0;
        }

        void print_info(wifi_manager_t*) {}

    }  // namespace nnet
}  // namespace ncore

#endif  // TARGET_ESP32
