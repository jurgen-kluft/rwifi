#include "rwifi/c_wifi_mgr_esp8266.h"

#ifdef TARGET_ESP8266

#include "ccore/c_memory.h"
#include "rcore/c_eeprom.h"

#include <ESP8266WiFi.h>

namespace ncore
{
    namespace nnet
    {
        struct wifi_esp8266_adapter_t
        {
            WiFiEventHandler m_got_ip_handler;
            WiFiEventHandler m_disconnected_handler;
            wifi_link_info_t m_link_info;
            wifi_event_t     m_events[8];
            u32              m_event_read = 0;
            u32              m_event_write = 0;
            u32              m_event_count = 0;
            bool             m_initialized = false;
        };

        static void push_event(wifi_esp8266_adapter_t* adapter, wifi_event_type_t type)
        {
            if (adapter->m_event_count == 8)
                return;

            adapter->m_events[adapter->m_event_write].m_type = type;
            adapter->m_event_write = (adapter->m_event_write + 1) % 8;
            adapter->m_event_count++;
        }

        static void update_link_info(wifi_esp8266_adapter_t* adapter)
        {
            wifi_link_info_t& info = adapter->m_link_info;
            g_memzero(&info, sizeof(info));
            info.m_cache.ip_address = WiFi.localIP();
            info.m_cache.ip_gateway = WiFi.gatewayIP();
            info.m_cache.ip_mask = WiFi.subnetMask();
            info.m_cache.ip_dns1 = WiFi.dnsIP(0);
            info.m_cache.ip_dns2 = WiFi.dnsIP(1);
            info.m_cache.wifi_channel = WiFi.channel();
            g_memcpy(info.m_cache.wifi_bssid, WiFi.BSSID(), 6);
            info.m_rssi = WiFi.RSSI();
        }

        static bool adapter_init(void* context)
        {
            wifi_esp8266_adapter_t* adapter = (wifi_esp8266_adapter_t*)context;
            if (adapter->m_initialized)
                return true;

            WiFi.setAutoReconnect(false);
            WiFi.persistent(true);
            if (!WiFi.mode(WIFI_STA))
                return false;

            adapter->m_got_ip_handler = WiFi.onStationModeGotIP([adapter](const WiFiEventStationModeGotIP&) {
                update_link_info(adapter);
                push_event(adapter, WIFI_EVENT_GOT_IP);
            });
            adapter->m_disconnected_handler = WiFi.onStationModeDisconnected([adapter](const WiFiEventStationModeDisconnected&) {
                push_event(adapter, WIFI_EVENT_DISCONNECTED);
            });
            adapter->m_initialized = true;
            return true;
        }

        static void adapter_shutdown(void* context)
        {
            wifi_esp8266_adapter_t* adapter = (wifi_esp8266_adapter_t*)context;
            adapter->m_got_ip_handler = WiFiEventHandler();
            adapter->m_disconnected_handler = WiFiEventHandler();
            adapter->m_initialized = false;
        }

        static bool adapter_start(void*) { return true; }
        static void adapter_stop(void*) { WiFi.disconnect(true); }

        static bool adapter_connect(void*, const wifi_connect_request_t* request)
        {
            if (request->m_fast_connect)
            {
                if (!WiFi.config(request->m_cache.ip_address, request->m_cache.ip_gateway, request->m_cache.ip_mask, request->m_cache.ip_dns1, request->m_cache.ip_dns2))
                    return false;
                WiFi.begin(request->m_ssid, request->m_password, request->m_cache.wifi_channel, request->m_cache.wifi_bssid, true);
            }
            else
            {
                WiFi.config(0U, 0U, 0U);
                WiFi.begin(request->m_ssid, request->m_password);
            }
            return true;
        }

        static void adapter_disconnect(void*) { WiFi.disconnect(false); }

        static bool adapter_poll_event(void* context, wifi_event_t* event)
        {
            wifi_esp8266_adapter_t* adapter = (wifi_esp8266_adapter_t*)context;
            if (adapter->m_event_count == 0)
                return false;

            *event = adapter->m_events[adapter->m_event_read];
            adapter->m_event_read = (adapter->m_event_read + 1) % 8;
            adapter->m_event_count--;
            return true;
        }

        static bool adapter_get_link_info(void* context, wifi_link_info_t* info)
        {
            *info = ((wifi_esp8266_adapter_t*)context)->m_link_info;
            return info->m_cache.ip_address != 0;
        }

        static bool adapter_get_mac_address(void*, u8* mac_address)
        {
            WiFi.macAddress(mac_address);
            return true;
        }

        static bool adapter_load_cache(void*, wifi_cache_t* cache)
        {
            return neeprom::load((byte*)cache, sizeof(wifi_cache_t));
        }

        static void adapter_save_cache(void*, const wifi_cache_t* cache)
        {
            neeprom::save((const byte*)cache, sizeof(wifi_cache_t));
        }

        wifi_esp8266_adapter_t* create_wifi_esp8266_adapter()
        {
            wifi_esp8266_adapter_t* adapter = new wifi_esp8266_adapter_t();
            g_memzero(&adapter->m_link_info, sizeof(adapter->m_link_info));
            return adapter;
        }

        void destroy_wifi_esp8266_adapter(wifi_esp8266_adapter_t* adapter)
        {
            if (!adapter)
                return;

            adapter_shutdown(adapter);
            delete adapter;
        }

        wifi_platform_ops_t get_wifi_esp8266_platform_ops(wifi_esp8266_adapter_t* adapter)
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

#endif  // TARGET_ESP8266
