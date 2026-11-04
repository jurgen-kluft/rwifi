#include "rcore/c_network.secret.h"
#include "rcore/c_eeprom.h"
#include "ccore/c_memory.h"
#include "rcore/c_log.h"

#include "rwifi/c_wifi_mgr.h"
#include "rwifi/c_wifi.h"

#ifdef TARGET_ARDUINO

namespace ncore
{
    namespace nnet
    {
        bool load_cache_from_eeprom(wifi_cache_t& cache)
        {
            ncore::neeprom::load((byte*)&cache, sizeof(wifi_cache_t));
            {
                const u32 crc = cache.m_crc;
                cache.m_crc   = 0;
                if (crc != neeprom::crc32((const byte*)&cache, sizeof(wifi_cache_t)))
                {
                    ncore::nlog::println(" WiFi cache in EEPROM is corrupted (CRC mismatch)");
                    cache.reset();
                    return false;
                }
                else
                {
                    ncore::nlog::println("WiFi cache loaded from EEPROM");
                    cache.m_crc = crc;
                    return true;
                }
            }
        }
    }  // namespace nnet
}  // namespace ncore

#    if defined(TARGET_ESP32) && 0

#        include "esp_wifi.h"
#        include "esp_event.h"
#        include "esp_log.h"
#        include "esp_random.h"  // Required for hardware random number generation
#        include "esp_netif.h"
#        include "freertos/FreeRTOS.h"
#        include "freertos/task.h"
#        include <string.h>

namespace ncore
{
    namespace nnet
    {
        struct wifi_manager_t
        {
            // config
            const char* ssid;
            const char* password;
            u32         init_backoff_ms;
            u32         max_backoff_ms;
            f32         backoff_multiplier;
            f32         jitter_percentage;  // e.g., 0.25f for +/- 25% variation

            esp_netif_t* netif_sta;

            wifi_cache_t m_cache;

            bool m_has_cached_mac;
            u8   m_mac_address[6];
            bool m_has_cached_ip;
            u8   m_ip_address[4];
            i32  m_rssi;

            // runtime
            wifi_state_t m_state;
            u32          m_last_attempt_ms;
            u32          m_backoff_ms;
            u32          m_current_wait_ms;  // Stores backoff_ms adjusted with jitter
            bool         m_connect_requested;
            bool         m_use_fast_connect;
        };

        static void schedule_backoff(wifi_manager_t* m, u32 now)
        {
            u32 next_backoff = (u32)(m->m_backoff_ms * m->backoff_multiplier);
            m->m_backoff_ms  = (next_backoff > m->max_backoff_ms) ? m->max_backoff_ms : next_backoff;

            i32 max_jitter = (i32)(m->m_backoff_ms * m->jitter_percentage);
            i32 jitter     = 0;
            if (max_jitter > 0)
            {
                jitter = ((i32)(esp_random() % (max_jitter * 2 + 1))) - max_jitter;
            }

            i32 total_wait = (i32)m->m_backoff_ms + jitter;
            if (total_wait < (i32)m->init_backoff_ms)
                total_wait = m->init_backoff_ms;
            if (total_wait > (i32)m->max_backoff_ms)
                total_wait = m->max_backoff_ms;

            m->m_current_wait_ms = (u32)total_wait;
            m->m_state           = WIFI_STATE_BACKOFF;
            m->m_last_attempt_ms = now;

            nlog::log_warnf("WIFI", "Connection dropped. Backing off for %lu ms (Jittered from %lu ms)", va_list_t(va_t(m->m_current_wait_ms), va_t(m->m_backoff_ms)));
        }

        static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
        {
            if (arg == nullptr)
                return;

            wifi_manager_t* wifi_mgr = (wifi_manager_t*)arg;

            if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
            {
                if (wifi_mgr->m_state != WIFI_STATE_INACTIVE && wifi_mgr->m_state != WIFI_STATE_BACKOFF)
                {
                    if (wifi_mgr->m_state == WIFI_STATE_CONNECTING && wifi_mgr->m_use_fast_connect)
                    {
                        wifi_mgr->m_use_fast_connect = false;
                        wifi_mgr->m_state            = WIFI_STATE_DISCONNECTED;
                    }
                    else if (wifi_mgr->m_state == WIFI_STATE_CONNECTING || wifi_mgr->m_state == WIFI_STATE_CONNECTED)
                    {
                        schedule_backoff(wifi_mgr, pdTICKS_TO_MS(xTaskGetTickCount()));
                    }
                    else
                    {
                        wifi_mgr->m_state = WIFI_STATE_DISCONNECTED;
                    }
                }
            }
            else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
            {
                wifi_mgr->m_state      = WIFI_STATE_CONNECTED;
                wifi_mgr->m_backoff_ms = wifi_mgr->init_backoff_ms;

                // Dynamically snapshot connection stats to update our runtime cache data structure
                ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
                wifi_cache_t&      cache = wifi_mgr->m_cache;

                cache.ip_address = event->ip_info.ip.addr;
                cache.ip_gateway = event->ip_info.gw.addr;
                cache.ip_mask    = event->ip_info.netmask.addr;

                // Retrieve DNS configurations from the netif layer
                esp_netif_dns_info_t dns_info;
                if (esp_netif_get_dns_info(wifi_mgr->netif_sta, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK)
                {
                    cache.ip_dns1 = dns_info.ip.u_addr.ip4.addr;
                }
                if (esp_netif_get_dns_info(wifi_mgr->netif_sta, ESP_NETIF_DNS_BACKUP, &dns_info) == ESP_OK)
                {
                    cache.ip_dns2 = dns_info.ip.u_addr.ip4.addr;
                }

                // Fetch physical access point details (Channel & BSSID)
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
                {
                    cache.wifi_channel = ap_info.primary;
                    memcpy(cache.wifi_bssid, ap_info.bssid, 6);
                }

                wifi_mgr->m_rssi = ap_info.rssi;

                wifi_mgr->m_has_cached_ip = true;
                wifi_mgr->m_ip_address[0] = cache.ip_address & 0xFF;
                wifi_mgr->m_ip_address[1] = (cache.ip_address >> 8) & 0xFF;
                wifi_mgr->m_ip_address[2] = (cache.ip_address >> 16) & 0xFF;
                wifi_mgr->m_ip_address[3] = (cache.ip_address >> 24) & 0xFF;

                wifi_mgr->m_has_cached_mac = true;
                wifi_mgr->m_mac_address[0] = ap_info.bssid[0];
                wifi_mgr->m_mac_address[1] = ap_info.bssid[1];
                wifi_mgr->m_mac_address[2] = ap_info.bssid[2];
                wifi_mgr->m_mac_address[3] = ap_info.bssid[3];
                wifi_mgr->m_mac_address[4] = ap_info.bssid[4];
                wifi_mgr->m_mac_address[5] = ap_info.bssid[5];

                // Save your persistent structure using your internal tools here:
                neeprom::save((const byte*)&cache, sizeof(wifi_cache_t));
            }
        }

        wifi_manager_t* create_wifi_mgr(const char* ssid, const char* password, u32 init_backoff_ms, u32 max_backoff_ms, f32 backoff_multiplier, f32 jitter_percentage)
        {
            wifi_manager_t* m = new wifi_manager_t();

            m->ssid               = ssid;
            m->password           = password;
            m->init_backoff_ms    = init_backoff_ms;
            m->max_backoff_ms     = max_backoff_ms;
            m->backoff_multiplier = backoff_multiplier;  // Exponential backoff multiplier
            m->jitter_percentage  = jitter_percentage;   // Applies up to a +/- 25% random adjustment to the sleep window

            m->m_has_cached_mac    = false;
            m->m_state             = WIFI_STATE_INACTIVE;
            m->m_backoff_ms        = init_backoff_ms;
            m->m_current_wait_ms   = init_backoff_ms;
            m->m_connect_requested = false;

            wifi_cache_t loaded_cache;
            if (load_cache_from_eeprom(loaded_cache) && loaded_cache.ip_address != 0)
            {
                m->m_cache            = loaded_cache;
                m->m_use_fast_connect = true;
            }
            else
            {
                memset(&m->m_cache, 0, sizeof(wifi_cache_t));
                m->m_use_fast_connect = false;
            }

            ESP_ERROR_CHECK(esp_netif_init());
            m->netif_sta = esp_netif_create_default_wifi_sta();

            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            ESP_ERROR_CHECK(esp_wifi_init(&cfg));

            ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, m, NULL));
            ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, m, NULL));

            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

            return m;
        }

        void activate(wifi_manager_t* m)
        {
            if (m->m_state == WIFI_STATE_INACTIVE)
            {
                m->m_connect_requested = true;
                m->m_state             = WIFI_STATE_DISCONNECTED;
                ESP_ERROR_CHECK(esp_wifi_start());
            }
        }

        void deactivate(wifi_manager_t* m)
        {
            m->m_connect_requested = false;
            m->m_state             = WIFI_STATE_INACTIVE;
            esp_wifi_disconnect();
            esp_wifi_stop();
        }

        void tick(wifi_manager_t* m)
        {
            if (!m->m_connect_requested)
                return;

            u32 now = pdTICKS_TO_MS(xTaskGetTickCount());

            switch (m->m_state)
            {
                case WIFI_STATE_DISCONNECTED:
                {
                    m->m_state           = WIFI_STATE_CONNECTING;
                    m->m_last_attempt_ms = now;

                    ::wifi_config_t wifi_cfg = {};
                    strncpy((char*)wifi_cfg.sta.ssid, m->ssid, sizeof(wifi_cfg.sta.ssid));
                    strncpy((char*)wifi_cfg.sta.password, m->password, sizeof(wifi_cfg.sta.password));

                    if (m->m_use_fast_connect && m->m_cache.ip_address != 0)
                    {
                        nlog::log_infof("WIFI", "Fast-connecting to %s...", va_list_t(va_t(m->ssid)));

                        // 1. Inject Static IP
                        //  configurations to bypass DHCP steps entirely
                        esp_netif_dhcpc_stop(m->netif_sta);
                        esp_netif_ip_info_t ip_info = {.ip = m->m_cache.ip_address, .netmask = m->m_cache.ip_mask, .gw = m->m_cache.ip_gateway};
                        esp_netif_set_ip_info(m->netif_sta, &ip_info);

                        // Inject DNS configurations manually
                        esp_netif_dns_info_t dns;
                        dns.ip.type            = ESP_IPADDR_TYPE_V4;
                        dns.ip.u_addr.ip4.addr = m->m_cache.ip_dns1;
                        esp_netif_set_dns_info(m->netif_sta, ESP_NETIF_DNS_MAIN, &dns);
                        if (m->m_cache.ip_dns2 != 0)
                        {
                            dns.ip.u_addr.ip4.addr = m->m_cache.ip_dns2;
                            esp_netif_set_dns_info(m->netif_sta, ESP_NETIF_DNS_BACKUP, &dns);
                        }

                        // 2. Bound station credentials strictly to the stored channel/BSSID metrics
                        wifi_cfg.sta.bssid_set = 1;
                        memcpy(wifi_cfg.sta.bssid, m->m_cache.wifi_bssid, 6);
                        wifi_cfg.sta.channel     = m->m_cache.wifi_channel;
                        wifi_cfg.sta.scan_method = WIFI_FAST_SCAN;
                    }
                    else
                    {
                        nlog::log_infof("WIFI", "Normal-connecting to %s...", va_list_t(va_t(m->ssid)));

                        // Rollback network defaults for a clean channel sweep
                        esp_netif_dhcpc_start(m->netif_sta);
                        wifi_cfg.sta.bssid_set   = 0;
                        wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
                    }

                    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
                    esp_wifi_connect();
                    break;
                }

                case WIFI_STATE_CONNECTING:
                {
                    u32 timeout_limit = m->m_use_fast_connect ? 4000 : 30000;

                    if ((now - m->m_last_attempt_ms) > timeout_limit)
                    {
                        if (m->m_use_fast_connect)
                        {
                            nlog::log_warn("WIFI", "Fast connect failed/timed out. Falling back to normal.");
                            m->m_use_fast_connect = false;
                            m->m_state            = WIFI_STATE_DISCONNECTED;
                        }
                        else
                        {
                            schedule_backoff(m, now);
                        }
                    }
                    break;
                }

                case WIFI_STATE_BACKOFF:
                    if ((now - m->m_last_attempt_ms) >= m->m_current_wait_ms)
                    {
                        m->m_use_fast_connect = false;
                        m->m_state            = WIFI_STATE_DISCONNECTED;
                    }
                    break;

                case WIFI_STATE_CONNECTED: break;

                case WIFI_STATE_INACTIVE:
                default: break;
            }
        }

        bool is_connected(const wifi_manager_t* m)
        {
            // Check if the Wi-Fi manager is currently connected to a network
            return m->m_state == WIFI_STATE_CONNECTED;
        }

        u8 const* get_ip_address(wifi_manager_t* m)
        {
            if (m->m_has_cached_ip)
                return m->m_ip_address;

            // use esp-idf to obtain the current IP address if not cached (e.g., on first boot or if cache is invalid)
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(m->netif_sta, &ip_info) == ESP_OK)
            {
                m->m_ip_address[0] = ip_info.ip.addr & 0xFF;
                m->m_ip_address[1] = (ip_info.ip.addr >> 8) & 0xFF;
                m->m_ip_address[2] = (ip_info.ip.addr >> 16) & 0xFF;
                m->m_ip_address[3] = (ip_info.ip.addr >> 24) & 0xFF;
                m->m_has_cached_ip = true;
                return m->m_ip_address;
            }

            // zero out the IP address on failure to avoid returning uninitialized data
            g_memset(m->m_ip_address, 0, 4);
            return m->m_ip_address;
        }

        u8 const* get_mac_address(wifi_manager_t* m)
        {
            if (m->m_has_cached_mac)
                return m->m_mac_address;

            // esp_wifi_get_mac gets the factory MAC address bound to the STA interface
            esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, m->m_mac_address);
            if (err == ESP_OK)
            {
                m->m_has_cached_mac = true;
            }
            else
            {
                // zero out the MAC address on failure to avoid returning uninitialized data
                g_memset(m->m_mac_address, 0, 6);
            }
            return m->m_mac_address;
        }

        i32 get_rssi(wifi_manager_t* m) { return m->m_rssi; }

        void print_info(wifi_manager_t* m)
        {
            // ncore::nlog::log_info("WIFI", "Connection Info:");
            // ncore::nlog::printvln("WIFI", "  SSID: ", m->ssid);
            // ncore::nlog::print("WIFI", "  MAC Address: ");
            // ncore::nlog::println_mac("WIFI", m->m_mac_address);
            // ncore::nlog::print("WIFI", "  IP Address: ");
            // ncore::nlog::println_ip("WIFI", m->m_ip_address);

            // ncore::nlog::printfln("  RSSI: %d dBm", va_list_t(va_t(m->m_rssi)));
        }

    }  // namespace nnet
}  // namespace ncore

#    endif

#    ifdef TARGET_ESP8266

#        include <ESP8266WiFi.h>

namespace ncore
{
    namespace nnet
    {
        struct wifi_manager_t
        {
            const char* ssid;
            const char* password;
            u32         init_backoff_ms;
            u32         max_backoff_ms;
            f32         backoff_multiplier;
            f32         jitter_percentage;

            wifi_cache_t m_cache;

            bool m_has_cached_mac;
            u8   m_mac_address[6];
            bool m_has_cached_ip;
            u8   m_ip_address[4];
            i32  m_rssi;

            wifi_state_t m_state;
            u32          m_last_attempt_ms;
            u32          m_backoff_ms;
            u32          m_current_wait_ms;
            bool         m_connect_requested;
            bool         m_use_fast_connect;

            WiFiEventHandler m_got_ip_handler;
            WiFiEventHandler m_disconnected_handler;
        };

        static void schedule_backoff(wifi_manager_t* m, u32 now)
        {
            u32 next_backoff = (u32)(m->m_backoff_ms * m->backoff_multiplier);
            m->m_backoff_ms  = (next_backoff > m->max_backoff_ms) ? m->max_backoff_ms : next_backoff;

            i32 max_jitter = (i32)(m->m_backoff_ms * m->jitter_percentage);
            i32 jitter     = 0;
            if (max_jitter > 0)
            {
                jitter = ((i32)(random() % (max_jitter * 2 + 1))) - max_jitter;
            }

            i32 total_wait = (i32)m->m_backoff_ms + jitter;
            if (total_wait < (i32)m->init_backoff_ms)
                total_wait = m->init_backoff_ms;
            if (total_wait > (i32)m->max_backoff_ms)
                total_wait = m->max_backoff_ms;

            m->m_current_wait_ms = (u32)total_wait;
            m->m_state           = WIFI_STATE_BACKOFF;
            m->m_last_attempt_ms = now;
        }

        static void on_wifi_got_ip(wifi_manager_t* m, const WiFiEventStationModeGotIP&)
        {
            if (!m->m_connect_requested)
                return;

            m->m_state      = WIFI_STATE_CONNECTED;
            m->m_backoff_ms = m->init_backoff_ms;

            wifi_cache_t& cache = m->m_cache;
            cache.ip_address    = WiFi.localIP();
            cache.ip_gateway    = WiFi.gatewayIP();
            cache.ip_mask       = WiFi.subnetMask();
            cache.ip_dns1       = WiFi.dnsIP(0);
            cache.ip_dns2       = WiFi.dnsIP(1);
            cache.wifi_channel  = WiFi.channel();
            memcpy(cache.wifi_bssid, WiFi.BSSID(), 6);

            get_ip_address(m);
            get_mac_address(m);
            m->m_rssi = WiFi.RSSI();

            neeprom::save((const byte*)&cache, sizeof(wifi_cache_t));
        }

        static void on_wifi_disconnect(wifi_manager_t* m, const WiFiEventStationModeDisconnected&)
        {
            if (!m->m_connect_requested || m->m_state == WIFI_STATE_INACTIVE || m->m_state == WIFI_STATE_BACKOFF)
                return;

            if (m->m_state == WIFI_STATE_CONNECTING && m->m_use_fast_connect)
            {
                m->m_use_fast_connect = false;
                m->m_state            = WIFI_STATE_DISCONNECTED;
            }
            else if (m->m_state == WIFI_STATE_CONNECTING || m->m_state == WIFI_STATE_CONNECTED)
            {
                schedule_backoff(m, millis());
            }
            else
            {
                m->m_state = WIFI_STATE_DISCONNECTED;
            }
        }

        wifi_manager_t* create_wifi_mgr(const char* ssid, const char* password, u32 init_backoff_ms, u32 max_backoff_ms, f32 backoff_multiplier, f32 jitter_percentage)
        {
            wifi_manager_t* m = new wifi_manager_t();

            m->ssid               = ssid;
            m->password           = password;
            m->init_backoff_ms    = init_backoff_ms;
            m->max_backoff_ms     = max_backoff_ms;
            m->backoff_multiplier = backoff_multiplier;  // Exponential backoff multiplier
            m->jitter_percentage  = jitter_percentage;   // Applies up to a +/- 25% random adjustment to the sleep window

            m->m_has_cached_mac    = false;
            m->m_has_cached_ip     = false;
            memset(m->m_mac_address, 0, sizeof(m->m_mac_address));
            memset(m->m_ip_address, 0, sizeof(m->m_ip_address));
            m->m_rssi              = 0;
            m->m_state             = WIFI_STATE_INACTIVE;
            m->m_last_attempt_ms   = 0;
            m->m_backoff_ms        = init_backoff_ms;
            m->m_current_wait_ms   = init_backoff_ms;
            m->m_connect_requested = false;

            wifi_cache_t loaded_cache;
            if (load_cache_from_eeprom(loaded_cache) && loaded_cache.ip_address != 0)
            {
                m->m_cache            = loaded_cache;
                m->m_use_fast_connect = true;
            }
            else
            {
                memset(&m->m_cache, 0, sizeof(wifi_cache_t));
                m->m_use_fast_connect = false;
            }

            WiFi.setAutoReconnect(false);
            WiFi.persistent(true);
            WiFi.mode(WIFI_STA);

            return m;
        }

        void activate(wifi_manager_t* m)
        {
            if (m->m_state == WIFI_STATE_INACTIVE)
            {
                m->m_connect_requested = true;
                m->m_state             = WIFI_STATE_DISCONNECTED;
                m->m_got_ip_handler = WiFi.onStationModeGotIP([m](const WiFiEventStationModeGotIP& event) { on_wifi_got_ip(m, event); });
                m->m_disconnected_handler = WiFi.onStationModeDisconnected([m](const WiFiEventStationModeDisconnected& event) { on_wifi_disconnect(m, event); });
            }
        }

        void deactivate(wifi_manager_t* m)
        {
            m->m_connect_requested = false;
            m->m_state             = WIFI_STATE_INACTIVE;
            m->m_got_ip_handler    = WiFiEventHandler();
            m->m_disconnected_handler = WiFiEventHandler();
            WiFi.disconnect(true);
        }

        void tick(wifi_manager_t* m)
        {
            if (!m->m_connect_requested)
                return;

            u32 now = millis();

            switch (m->m_state)
            {
                case WIFI_STATE_DISCONNECTED:
                    m->m_state           = WIFI_STATE_CONNECTING;
                    m->m_last_attempt_ms = now;
                    if (m->m_use_fast_connect && m->m_cache.ip_address != 0)
                    {
                        // Execute optimized configuration mapping steps
                        WiFi.config(m->m_cache.ip_address, m->m_cache.ip_gateway, m->m_cache.ip_mask, m->m_cache.ip_dns1, m->m_cache.ip_dns2);
                        WiFi.begin(m->ssid, m->password, m->m_cache.wifi_channel, m->m_cache.wifi_bssid, true);
                    }
                    else
                    {
                        // Execute unmapped full channel fallback connection routine
                        WiFi.begin(m->ssid, m->password);
                    }
                    break;

                case WIFI_STATE_CONNECTING:
                    // Fast connect timeout is tighter (4 seconds), full connect timeout uses 30 seconds
                    u32 timeout_limit;
                    timeout_limit = m->m_use_fast_connect ? 4000 : 30000;

                    if ((now - m->m_last_attempt_ms) > timeout_limit)
                    {
                        // Fallback mechanics if fast path drops
                        if (m->m_use_fast_connect)
                        {
                            m->m_use_fast_connect = false;                    // Discard fast connect strategy on this cycle
                            m->m_state            = WIFI_STATE_DISCONNECTED;  // Force immediate normal connect retry
                        }
                        else
                        {
                            schedule_backoff(m, now);
                        }
                    }
                    break;

                case WIFI_STATE_BACKOFF:
                    if ((now - m->m_last_attempt_ms) >= m->m_current_wait_ms)
                    {
                        // When rising back from backoff, always assume normal mode to guarantee clean channel sweeps
                        m->m_use_fast_connect = false;
                        m->m_state            = WIFI_STATE_DISCONNECTED;
                    }
                    break;

                case WIFI_STATE_CONNECTED: m->m_rssi = WiFi.RSSI(); break;

                case WIFI_STATE_INACTIVE:
                default: break;
            }

        }

        bool is_connected(const wifi_manager_t* m) { return m->m_state == WIFI_STATE_CONNECTED; }

        u8 const* get_ip_address(wifi_manager_t* m)
        {
            if (m->m_has_cached_ip)
                return m->m_ip_address;

            // On ESP8266 Arduino, the local IP can be obtained directly from the WiFi library
            IPAddress ip       = WiFi.localIP();
            m->m_ip_address[0] = ip[0];
            m->m_ip_address[1] = ip[1];
            m->m_ip_address[2] = ip[2];
            m->m_ip_address[3] = ip[3];
            m->m_has_cached_ip = true;
            return m->m_ip_address;
        }

        u8 const* get_mac_address(wifi_manager_t* m)
        {
            if (m->m_has_cached_mac)
                return m->m_mac_address;

            WiFi.macAddress(m->m_mac_address);
            m->m_has_cached_mac = true;

            return m->m_mac_address;
        }

        i32 get_rssi(wifi_manager_t* m) { return m->m_rssi; }

        void print_info(wifi_manager_t* m)
        {
            ncore::nlog::println("WiFi Connection Info:");
            ncore::nlog::printvln("  SSID: ", m->ssid);
            ncore::nlog::print("  MAC Address: ");
            ncore::nlog::println_mac(m->m_mac_address);
            ncore::nlog::print("  IP Address: ");
            ncore::nlog::println_ip(m->m_ip_address);

            ncore::nlog::printfln("  RSSI: %d dBm", va_t(m->m_rssi));
        }

    }  // namespace nnet
}  // namespace ncore

#    endif

#endif
