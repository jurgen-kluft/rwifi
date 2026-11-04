#include "rwifi/c_wifi_mgr_esp32.h"

#ifdef TARGET_ESP32

#include "ccore/c_memory.h"
#include "rcore/c_eeprom.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"

#include <string.h>

namespace ncore
{
    namespace nnet
    {
        struct wifi_esp32_adapter_t
        {
            esp_netif_t*                  m_netif_sta;
            esp_event_handler_instance_t  m_wifi_handler;
            esp_event_handler_instance_t  m_ip_handler;
            wifi_link_info_t              m_link_info;
            wifi_event_t                  m_events[8];
            portMUX_TYPE                  m_event_lock = portMUX_INITIALIZER_UNLOCKED;
            u32                           m_event_read = 0;
            u32                           m_event_write = 0;
            u32                           m_event_count = 0;
            bool                          m_initialized = false;
        };

        static void push_event(wifi_esp32_adapter_t* adapter, wifi_event_type_t type)
        {
            portENTER_CRITICAL(&adapter->m_event_lock);
            if (adapter->m_event_count < 8)
            {
                adapter->m_events[adapter->m_event_write].m_type = type;
                adapter->m_event_write = (adapter->m_event_write + 1) % 8;
                adapter->m_event_count++;
            }
            portEXIT_CRITICAL(&adapter->m_event_lock);
        }

        static void update_link_info(wifi_esp32_adapter_t* adapter)
        {
            wifi_link_info_t& info = adapter->m_link_info;
            g_memzero(&info, sizeof(info));

            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(adapter->m_netif_sta, &ip_info) == ESP_OK)
            {
                info.m_cache.ip_address = ip_info.ip.addr;
                info.m_cache.ip_gateway = ip_info.gw.addr;
                info.m_cache.ip_mask    = ip_info.netmask.addr;
            }

            esp_netif_dns_info_t dns_info;
            if (esp_netif_get_dns_info(adapter->m_netif_sta, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK)
                info.m_cache.ip_dns1 = dns_info.ip.u_addr.ip4.addr;
            if (esp_netif_get_dns_info(adapter->m_netif_sta, ESP_NETIF_DNS_BACKUP, &dns_info) == ESP_OK)
                info.m_cache.ip_dns2 = dns_info.ip.u_addr.ip4.addr;

            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
            {
                info.m_cache.wifi_channel = ap_info.primary;
                g_memcpy(info.m_cache.wifi_bssid, ap_info.bssid, 6);
                info.m_rssi = ap_info.rssi;
            }
        }

        static void wifi_event_handler(void* argument, esp_event_base_t event_base, int32_t event_id, void*)
        {
            wifi_esp32_adapter_t* adapter = (wifi_esp32_adapter_t*)argument;
            if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
                push_event(adapter, WIFI_EVENT_DISCONNECTED);
            else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
            {
                update_link_info(adapter);
                push_event(adapter, WIFI_EVENT_GOT_IP);
            }
        }

        static bool adapter_init(void* context)
        {
            wifi_esp32_adapter_t* adapter = (wifi_esp32_adapter_t*)context;
            if (adapter->m_initialized)
                return true;

            esp_err_t err = esp_netif_init();
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
                return false;

            adapter->m_netif_sta = esp_netif_create_default_wifi_sta();
            if (!adapter->m_netif_sta)
                return false;

            wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
            if (esp_wifi_init(&wifi_config) != ESP_OK)
                return false;
            if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
                return false;
            if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, adapter, &adapter->m_wifi_handler) != ESP_OK)
                return false;
            if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, adapter, &adapter->m_ip_handler) != ESP_OK)
                return false;

            adapter->m_initialized = true;
            return true;
        }

        static void adapter_shutdown(void* context)
        {
            wifi_esp32_adapter_t* adapter = (wifi_esp32_adapter_t*)context;
            if (!adapter->m_initialized)
                return;

            esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, adapter->m_wifi_handler);
            esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, adapter->m_ip_handler);
            esp_wifi_stop();
            esp_wifi_deinit();
            if (adapter->m_netif_sta)
                esp_netif_destroy_default_wifi(adapter->m_netif_sta);
            adapter->m_netif_sta = nullptr;
            adapter->m_initialized = false;
        }

        static bool adapter_start(void*)
        {
            const esp_err_t err = esp_wifi_start();
            return err == ESP_OK || err == ESP_ERR_WIFI_NOT_STOPPED;
        }

        static void adapter_stop(void*)
        {
            esp_wifi_stop();
        }

        static bool adapter_connect(void* context, const wifi_connect_request_t* request)
        {
            wifi_esp32_adapter_t* adapter = (wifi_esp32_adapter_t*)context;
            ::wifi_config_t wifi_config = {};
            strncpy((char*)wifi_config.sta.ssid, request->m_ssid, sizeof(wifi_config.sta.ssid) - 1);
            strncpy((char*)wifi_config.sta.password, request->m_password, sizeof(wifi_config.sta.password) - 1);

            if (request->m_fast_connect)
            {
                esp_netif_dhcpc_stop(adapter->m_netif_sta);
                esp_netif_ip_info_t ip_info = {.ip = request->m_cache.ip_address, .netmask = request->m_cache.ip_mask, .gw = request->m_cache.ip_gateway};
                if (esp_netif_set_ip_info(adapter->m_netif_sta, &ip_info) != ESP_OK)
                    return false;

                esp_netif_dns_info_t dns = {};
                dns.ip.type = ESP_IPADDR_TYPE_V4;
                dns.ip.u_addr.ip4.addr = request->m_cache.ip_dns1;
                if (esp_netif_set_dns_info(adapter->m_netif_sta, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK)
                    return false;
                if (request->m_cache.ip_dns2 != 0)
                {
                    dns.ip.u_addr.ip4.addr = request->m_cache.ip_dns2;
                    if (esp_netif_set_dns_info(adapter->m_netif_sta, ESP_NETIF_DNS_BACKUP, &dns) != ESP_OK)
                        return false;
                }

                wifi_config.sta.bssid_set = 1;
                g_memcpy(wifi_config.sta.bssid, request->m_cache.wifi_bssid, 6);
                wifi_config.sta.channel = request->m_cache.wifi_channel;
                wifi_config.sta.scan_method = WIFI_FAST_SCAN;
            }
            else
            {
                esp_netif_dhcpc_start(adapter->m_netif_sta);
                wifi_config.sta.bssid_set = 0;
                wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
            }

            return esp_wifi_set_config(WIFI_IF_STA, &wifi_config) == ESP_OK && esp_wifi_connect() == ESP_OK;
        }

        static void adapter_disconnect(void*)
        {
            esp_wifi_disconnect();
        }

        static bool adapter_poll_event(void* context, wifi_event_t* event)
        {
            wifi_esp32_adapter_t* adapter = (wifi_esp32_adapter_t*)context;
            portENTER_CRITICAL(&adapter->m_event_lock);
            if (adapter->m_event_count == 0)
            {
                portEXIT_CRITICAL(&adapter->m_event_lock);
                return false;
            }

            *event = adapter->m_events[adapter->m_event_read];
            adapter->m_event_read = (adapter->m_event_read + 1) % 8;
            adapter->m_event_count--;
            portEXIT_CRITICAL(&adapter->m_event_lock);
            return true;
        }

        static bool adapter_get_link_info(void* context, wifi_link_info_t* info)
        {
            *info = ((wifi_esp32_adapter_t*)context)->m_link_info;
            return info->m_cache.ip_address != 0;
        }

        static bool adapter_get_mac_address(void*, u8* mac_address)
        {
            return esp_wifi_get_mac(WIFI_IF_STA, mac_address) == ESP_OK;
        }

        static bool adapter_load_cache(void*, wifi_cache_t* cache)
        {
            return neeprom::load((byte*)cache, sizeof(wifi_cache_t));
        }

        static void adapter_save_cache(void*, const wifi_cache_t* cache)
        {
            neeprom::save((const byte*)cache, sizeof(wifi_cache_t));
        }

        wifi_esp32_adapter_t* create_wifi_esp32_adapter()
        {
            wifi_esp32_adapter_t* adapter = new wifi_esp32_adapter_t();
            g_memzero(&adapter->m_link_info, sizeof(adapter->m_link_info));
            return adapter;
        }

        void destroy_wifi_esp32_adapter(wifi_esp32_adapter_t* adapter)
        {
            if (!adapter)
                return;

            adapter_shutdown(adapter);
            delete adapter;
        }

        wifi_platform_ops_t get_wifi_esp32_platform_ops(wifi_esp32_adapter_t* adapter)
        {
            wifi_platform_ops_t ops = {};
            ops.m_context = adapter;
            ops.m_init = adapter_init;
            ops.m_shutdown = adapter_shutdown;
            ops.m_start = adapter_start;
            ops.m_stop = adapter_stop;
            ops.m_connect = adapter_connect;
            ops.m_disconnect = adapter_disconnect;
            ops.m_poll_event = adapter_poll_event;
            ops.m_get_link_info = adapter_get_link_info;
            ops.m_get_mac_address = adapter_get_mac_address;
            ops.m_load_cache = adapter_load_cache;
            ops.m_save_cache = adapter_save_cache;
            return ops;
        }

    }  // namespace nnet
}  // namespace ncore

#endif  // TARGET_ESP32
