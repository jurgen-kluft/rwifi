#include "rwifi/c_wifi_mgr.h"
#include "rcore/c_network.secret.h"
#include "rcore/c_eeprom.h"
#include "ccore/c_memory.h"
#include "rcore/c_log.h"

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

#    ifdef TARGET_ESP32

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
        void init_wifi_config(wifi_config_t& config, const char* ssid, const char* password, u32 init_backoff_ms, u32 max_backoff_ms, f32 backoff_multiplier, f32 jitter_percentage)
        {
            config.ssid               = ssid;
            config.password           = password;
            config.init_backoff_ms    = init_backoff_ms;
            config.max_backoff_ms     = max_backoff_ms;
            config.backoff_multiplier = backoff_multiplier;  // Exponential backoff multiplier
            config.jitter_percentage  = jitter_percentage;   // Applies up to a +/- 25% random adjustment to the sleep window
        }

        // Global instance pointer for event handler mapping
        static wifi_manager_t* g_wifi_mgr  = NULL;
        static esp_netif_t*    g_netif_sta = NULL;

        static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
        {
            if (!g_wifi_mgr)
                return;

            if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
            {
                if (g_wifi_mgr->m_state != WIFI_STATE_INACTIVE && g_wifi_mgr->m_state != WIFI_STATE_BACKOFF)
                {
                    g_wifi_mgr->m_state = WIFI_STATE_DISCONNECTED;
                }
            }
            else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
            {
                g_wifi_mgr->m_state      = WIFI_STATE_CONNECTED;
                g_wifi_mgr->m_backoff_ms = g_wifi_mgr->m_config->init_backoff_ms;

                // Dynamically snapshot connection stats to update our runtime cache data structure
                ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
                wifi_cache_t&      cache = g_wifi_mgr->m_cache;

                cache.ip_address = event->ip_info.ip.addr;
                cache.ip_gateway = event->ip_info.gw.addr;
                cache.ip_mask    = event->ip_info.netmask.addr;

                // Retrieve DNS configurations from the netif layer
                esp_netif_dns_info_t dns_info;
                if (esp_netif_get_dns_info(g_netif_sta, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK)
                {
                    cache.ip_dns1 = dns_info.ip.u_addr.ip4.addr;
                }
                if (esp_netif_get_dns_info(g_netif_sta, ESP_NETIF_DNS_BACKUP, &dns_info) == ESP_OK)
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

                g_wifi_mgr->m_rssi = ap_info.rssi;

                g_wifi_mgr->m_has_cached_ip = true;
                g_wifi_mgr->m_ip_address[0] = cache.ip_address & 0xFF;
                g_wifi_mgr->m_ip_address[1] = (cache.ip_address >> 8) & 0xFF;
                g_wifi_mgr->m_ip_address[2] = (cache.ip_address >> 16) & 0xFF;
                g_wifi_mgr->m_ip_address[3] = (cache.ip_address >> 24) & 0xFF;

                g_wifi_mgr->m_has_cached_mac = true;
                g_wifi_mgr->m_mac_address[0] = ap_info.bssid[0];
                g_wifi_mgr->m_mac_address[1] = ap_info.bssid[1];
                g_wifi_mgr->m_mac_address[2] = ap_info.bssid[2];
                g_wifi_mgr->m_mac_address[3] = ap_info.bssid[3];
                g_wifi_mgr->m_mac_address[4] = ap_info.bssid[4];
                g_wifi_mgr->m_mac_address[5] = ap_info.bssid[5];

                // Save your persistent structure using your internal tools here:
                neeprom::save((const byte*)&cache, sizeof(wifi_cache_t));
            }
        }

        void setup(wifi_manager_t& m, const wifi_config_t* config)
        {
            g_wifi_mgr            = &m;
            m.m_config            = config;
            m.m_has_cached_mac    = false;
            m.m_state             = WIFI_STATE_INACTIVE;
            m.m_backoff_ms        = config->init_backoff_ms;
            m.m_current_wait_ms   = config->init_backoff_ms;
            m.m_connect_requested = false;

            wifi_cache_t loaded_cache;
            if (load_cache_from_eeprom(loaded_cache) && loaded_cache.ip_address != 0)
            {
                m.m_cache            = loaded_cache;
                m.m_use_fast_connect = true;
            }
            else
            {
                memset(&m.m_cache, 0, sizeof(wifi_cache_t));
                m.m_use_fast_connect = false;
            }

            ESP_ERROR_CHECK(esp_netif_init());
            g_netif_sta = esp_netif_create_default_wifi_sta();

            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            ESP_ERROR_CHECK(esp_wifi_init(&cfg));

            ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
            ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        }

        void activate(wifi_manager_t& m)
        {
            if (m.m_state == WIFI_STATE_INACTIVE)
            {
                m.m_connect_requested = true;
                m.m_state             = WIFI_STATE_DISCONNECTED;
                ESP_ERROR_CHECK(esp_wifi_start());
            }
        }

        void deactivate(wifi_manager_t& m)
        {
            m.m_connect_requested = false;
            m.m_state             = WIFI_STATE_INACTIVE;
            esp_wifi_disconnect();
            esp_wifi_stop();
        }

        void tick(wifi_manager_t& m)
        {
            if (!m.m_connect_requested)
                return;

            u32 now = pdTICKS_TO_MS(xTaskGetTickCount());

            switch (m.m_state)
            {
                case WIFI_STATE_DISCONNECTED:
                {
                    m.m_state           = WIFI_STATE_CONNECTING;
                    m.m_last_attempt_ms = now;

                    ::wifi_config_t wifi_cfg = {};
                    strncpy((char*)wifi_cfg.sta.ssid, m.m_config->ssid, sizeof(wifi_cfg.sta.ssid));
                    strncpy((char*)wifi_cfg.sta.password, m.m_config->password, sizeof(wifi_cfg.sta.password));

                    if (m.m_use_fast_connect && m.m_cache.ip_address != 0)
                    {
                        nlog::log_infof("WIFI", "Fast-connecting to %s...", va_list_t(va_t(m.m_config->ssid)));

                        // 1. Inject Static IP configurations to bypass DHCP steps entirely
                        esp_netif_dhcpc_stop(g_netif_sta);
                        esp_netif_ip_info_t ip_info = {.ip = m.m_cache.ip_address, .netmask = m.m_cache.ip_mask, .gw = m.m_cache.ip_gateway};
                        esp_netif_set_ip_info(g_netif_sta, &ip_info);

                        // Inject DNS configurations manually
                        esp_netif_dns_info_t dns;
                        dns.ip.type            = ESP_IPADDR_TYPE_V4;
                        dns.ip.u_addr.ip4.addr = m.m_cache.ip_dns1;
                        esp_netif_set_dns_info(g_netif_sta, ESP_NETIF_DNS_MAIN, &dns);

                        // 2. Bound station credentials strictly to the stored channel/BSSID metrics
                        wifi_cfg.sta.bssid_set = 1;
                        memcpy(wifi_cfg.sta.bssid, m.m_cache.wifi_bssid, 6);
                        wifi_cfg.sta.channel     = m.m_cache.wifi_channel;
                        wifi_cfg.sta.scan_method = WIFI_FAST_SCAN;
                    }
                    else
                    {
                        nlog::log_infof("WIFI", "Normal-connecting to %s...", va_list_t(va_t(m.m_config->ssid)));

                        // Rollback network defaults for a clean channel sweep
                        esp_netif_dhcpc_start(g_netif_sta);
                        wifi_cfg.sta.bssid_set   = 0;
                        wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
                    }

                    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
                    esp_wifi_connect();
                    break;
                }

                case WIFI_STATE_CONNECTING:
                {
                    u32 timeout_limit = m.m_use_fast_connect ? 4000 : 30000;

                    if ((now - m.m_last_attempt_ms) > timeout_limit)
                    {
                        if (m.m_use_fast_connect)
                        {
                            nlog::log_warn("WIFI", "Fast connect failed/timed out. Falling back to normal.");
                            m.m_use_fast_connect = false;
                            m.m_state            = WIFI_STATE_DISCONNECTED;
                        }
                        else
                        {
                            m.m_state           = WIFI_STATE_BACKOFF;
                            m.m_last_attempt_ms = now;
                        }
                    }
                    break;
                }

                case WIFI_STATE_BACKOFF:
                    if ((now - m.m_last_attempt_ms) >= m.m_current_wait_ms)
                    {
                        m.m_use_fast_connect = false;
                        m.m_state            = WIFI_STATE_DISCONNECTED;
                    }
                    break;

                case WIFI_STATE_CONNECTED: break;

                case WIFI_STATE_INACTIVE:
                default: break;
            }

            if (m.m_state == WIFI_STATE_CONNECTING && (g_wifi_mgr->m_state == WIFI_STATE_DISCONNECTED || g_wifi_mgr->m_state == WIFI_STATE_BACKOFF))
            {
                if (m.m_use_fast_connect)
                {
                    m.m_use_fast_connect = false;
                    m.m_state            = WIFI_STATE_DISCONNECTED;
                    return;
                }

                u32 next_backoff = (u32)(m.m_backoff_ms * m.m_config->backoff_multiplier);
                m.m_backoff_ms   = (next_backoff > m.m_config->max_backoff_ms) ? m.m_config->max_backoff_ms : next_backoff;

                i32 max_jitter = (i32)(m.m_backoff_ms * m.m_config->jitter_percentage);
                i32 jitter     = 0;
                if (max_jitter > 0)
                {
                    jitter = ((i32)(esp_random() % (max_jitter * 2 + 1))) - max_jitter;
                }

                i32 total_wait = (i32)m.m_backoff_ms + jitter;
                if (total_wait < (i32)m.m_config->init_backoff_ms)
                    total_wait = m.m_config->init_backoff_ms;
                if (total_wait > (i32)m.m_config->max_backoff_ms)
                    total_wait = m.m_config->max_backoff_ms;

                m.m_current_wait_ms = (u32)total_wait;
                m.m_state           = WIFI_STATE_BACKOFF;
                m.m_last_attempt_ms = now;

                nlog::log_warnf("WIFI", "Connection dropped. Backing off for %lu ms (Jittered from %lu ms)", va_list_t(va_t(m.m_current_wait_ms), va_t(m.m_backoff_ms)));
            }
        }

        u8 const* get_ip_address(wifi_manager_t& m)
        {
            if (m.m_has_cached_ip)
                return m.m_ip_address;

            // use esp-idf to obtain the current IP address if not cached (e.g., on first boot or if cache is invalid)
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(g_netif_sta, &ip_info) == ESP_OK)
            {
                m.m_ip_address[0] = ip_info.ip.addr & 0xFF;
                m.m_ip_address[1] = (ip_info.ip.addr >> 8) & 0xFF;
                m.m_ip_address[2] = (ip_info.ip.addr >> 16) & 0xFF;
                m.m_ip_address[3] = (ip_info.ip.addr >> 24) & 0xFF;
                m.m_has_cached_ip = true;
                return m.m_ip_address;
            }

            // zero out the IP address on failure to avoid returning uninitialized data
            g_memset(m.m_ip_address, 0, 4);
            return m.m_ip_address;
        }

        u8 const* get_mac_address(wifi_manager_t& m)
        {
            if (m.m_has_cached_mac)
                return m.m_mac_address;

            // esp_wifi_get_mac gets the factory MAC address bound to the STA interface
            esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, m.m_mac_address);
            if (err == ESP_OK)
            {
                m.m_has_cached_mac = true;
            }
            else
            {
                // zero out the MAC address on failure to avoid returning uninitialized data
                g_memset(m.m_mac_address, 0, 6);
            }
            return m.m_mac_address;
        }

        void print_info(wifi_manager_t& m)
        {
            // ncore::nlog::log_info("WIFI", "Connection Info:");
            // ncore::nlog::printvln("WIFI", "  SSID: ", m.m_config->ssid);
            // ncore::nlog::print("WIFI", "  MAC Address: ");
            // ncore::nlog::println_mac("WIFI", m.m_mac_address);
            // ncore::nlog::print("WIFI", "  IP Address: ");
            // ncore::nlog::println_ip("WIFI", m.m_ip_address);

            // ncore::nlog::printfln("  RSSI: %d dBm", va_list_t(va_t(m.m_rssi)));
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
        // Global instance pointer for Arduino event callbacks
        static wifi_manager_t*  g_wifi_mgr = NULL;
        static WiFiEventHandler g_got_ip_handler;
        static WiFiEventHandler g_disconnected_handler;

        static void on_wifi_got_ip(const WiFiEventStationModeGotIP& event)
        {
            if (!g_wifi_mgr)
                return;

            g_wifi_mgr->m_state      = WIFI_STATE_CONNECTED;
            g_wifi_mgr->m_backoff_ms = g_wifi_mgr->m_config->init_backoff_ms;

            // Dynamically snapshot and update current cache profile
            wifi_cache_t& cache = g_wifi_mgr->m_cache;
            cache.ip_address    = WiFi.localIP();
            cache.ip_gateway    = WiFi.gatewayIP();
            cache.ip_mask       = WiFi.subnetMask();
            cache.ip_dns1       = WiFi.dnsIP(0);
            cache.ip_dns2       = WiFi.dnsIP(1);
            cache.wifi_channel  = WiFi.channel();
            memcpy(cache.wifi_bssid, WiFi.BSSID(), 6);

            get_ip_address(*g_wifi_mgr);
            get_mac_address(*g_wifi_mgr);

            // Proactively save persistent data structures using your local EEPROM/Flash layer here:
            neeprom::save((const byte*)&cache, sizeof(wifi_cache_t));
        }

        static void on_wifi_disconnect(const WiFiEventStationModeDisconnected& event)
        {
            if (!g_wifi_mgr)
                return;

            if (g_wifi_mgr->m_state != WIFI_STATE_INACTIVE && g_wifi_mgr->m_state != WIFI_STATE_BACKOFF)
            {
                g_wifi_mgr->m_state = WIFI_STATE_DISCONNECTED;
            }
        }

        void setup(wifi_manager_t& m, const wifi_config_t* config)
        {
            g_wifi_mgr            = &m;
            m.m_config            = config;
            m.m_state             = WIFI_STATE_INACTIVE;
            m.m_backoff_ms        = config->init_backoff_ms;
            m.m_current_wait_ms   = config->init_backoff_ms;
            m.m_connect_requested = false;

            wifi_cache_t loaded_cache;
            if (load_cache_from_eeprom(loaded_cache) && loaded_cache.ip_address != 0)
            {
                m.m_cache            = loaded_cache;
                m.m_use_fast_connect = true;
            }
            else
            {
                memset(&m.m_cache, 0, sizeof(wifi_cache_t));
                m.m_use_fast_connect = false;
            }

            WiFi.setAutoReconnect(false);
            WiFi.persistent(true);
            WiFi.mode(WIFI_STA);

            // Register ESP8266 Arduino persistent event handlers
            g_got_ip_handler       = WiFi.onStationModeGotIP(&on_wifi_got_ip);
            g_disconnected_handler = WiFi.onStationModeDisconnected(&on_wifi_disconnect);
        }

        void activate(wifi_manager_t& m)
        {
            if (m.m_state == WIFI_STATE_INACTIVE)
            {
                m.m_connect_requested = true;
                m.m_state             = WIFI_STATE_DISCONNECTED;
            }
        }

        void deactivate(wifi_manager_t& m)
        {
            m.m_connect_requested = false;
            m.m_state             = WIFI_STATE_INACTIVE;
            WiFi.disconnect(true);
        }

        void tick(wifi_manager_t& m)
        {
            if (!m.m_connect_requested)
                return;

            u32 now = millis();

            switch (m.m_state)
            {
                case WIFI_STATE_DISCONNECTED:
                    m.m_state           = WIFI_STATE_CONNECTING;
                    m.m_last_attempt_ms = now;
                    if (m.m_use_fast_connect && m.m_cache.ip_address != 0)
                    {
                        // Execute optimized configuration mapping steps
                        WiFi.config(m.m_cache.ip_address, m.m_cache.ip_gateway, m.m_cache.ip_mask, m.m_cache.ip_dns1, m.m_cache.ip_dns2);
                        WiFi.begin(m.m_config->ssid, m.m_config->password, m.m_cache.wifi_channel, m.m_cache.wifi_bssid, true);
                    }
                    else
                    {
                        // Execute unmapped full channel fallback connection routine
                        WiFi.begin(m.m_config->ssid, m.m_config->password);
                    }
                    break;

                case WIFI_STATE_CONNECTING:
                    // Fast connect timeout is tighter (4 seconds), full connect timeout uses 30 seconds
                    u32 timeout_limit;
                    timeout_limit = m.m_use_fast_connect ? 4000 : 30000;

                    if ((now - m.m_last_attempt_ms) > timeout_limit)
                    {
                        // Fallback mechanics if fast path drops
                        if (m.m_use_fast_connect)
                        {
                            m.m_use_fast_connect = false;                    // Discard fast connect strategy on this cycle
                            m.m_state            = WIFI_STATE_DISCONNECTED;  // Force immediate normal connect retry
                        }
                        else
                        {
                            m.m_state           = WIFI_STATE_BACKOFF;
                            m.m_last_attempt_ms = now;
                        }
                    }
                    break;

                case WIFI_STATE_BACKOFF:
                    if ((now - m.m_last_attempt_ms) >= m.m_current_wait_ms)
                    {
                        // When rising back from backoff, always assume normal mode to guarantee clean channel sweeps
                        m.m_use_fast_connect = false;
                    }
                    break;

                case WIFI_STATE_CONNECTED:
                    // Obtain RSSI state
                    // esp_wifi_ap_record_t ap_info;
                    // if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
                    // {
                    //     m.m_rssi = ap_info.rssi;
                    // }
                    // else
                    // {
                    //     m.m_rssi = 0;  // Default to 0 if RSSI retrieval fails
                    // }
                    m.m_rssi = 0;  // Default to 0 if RSSI retrieval fails
                    break;

                case WIFI_STATE_INACTIVE:
                default: break;
            }

            // Dynamic processing block for unexpected asynchronous drop events
            if (m.m_state == WIFI_STATE_CONNECTING && (g_wifi_mgr->m_state == WIFI_STATE_DISCONNECTED || g_wifi_mgr->m_state == WIFI_STATE_BACKOFF))
            {
                // Cancel ongoing fast configurations permanently if handshake drop occurs early
                if (m.m_use_fast_connect)
                {
                    m.m_use_fast_connect = false;
                    m.m_state            = WIFI_STATE_DISCONNECTED;
                    return;
                }

                u32 next_backoff = (u32)(m.m_backoff_ms * m.m_config->backoff_multiplier);
                m.m_backoff_ms   = (next_backoff > m.m_config->max_backoff_ms) ? m.m_config->max_backoff_ms : next_backoff;

                i32 max_jitter = (i32)(m.m_backoff_ms * m.m_config->jitter_percentage);
                i32 jitter     = 0;
                if (max_jitter > 0)
                {
                    jitter = ((i32)(os_random() % (max_jitter * 2 + 1))) - max_jitter;
                }

                i32 total_wait = (i32)m.m_backoff_ms + jitter;
                if (total_wait < (i32)m.m_config->init_backoff_ms)
                    total_wait = m.m_config->init_backoff_ms;
                if (total_wait > (i32)m.m_config->max_backoff_ms)
                    total_wait = m.m_config->max_backoff_ms;

                m.m_current_wait_ms = (u32)total_wait;
                m.m_state           = WIFI_STATE_BACKOFF;
                m.m_last_attempt_ms = now;
            }
        }

        u8 const* get_ip_address(wifi_manager_t& m)
        {
            if (m.m_has_cached_ip)
                return m.m_ip_address;

            // On ESP8266 Arduino, the local IP can be obtained directly from the WiFi library
            IPAddress ip      = WiFi.localIP();
            m.m_ip_address[0] = ip[0];
            m.m_ip_address[1] = ip[1];
            m.m_ip_address[2] = ip[2];
            m.m_ip_address[3] = ip[3];
            m.m_has_cached_ip = true;
            return m.m_ip_address;
        }

        u8 const* get_mac_address(wifi_manager_t& m)
        {
            if (m.m_has_cached_mac)
                return m.m_mac_address;

            WiFi.macAddress(m.m_mac_address);
            m.m_has_cached_mac = true;

            return m.m_mac_address;
        }

        void print_info(wifi_manager_t& m)
        {
            ncore::nlog::println("WiFi Connection Info:");
            ncore::nlog::printvln("  SSID: ", m.m_config->ssid);
            ncore::nlog::print("  MAC Address: ");
            ncore::nlog::println_mac(m.m_mac_address);
            ncore::nlog::print("  IP Address: ");
            ncore::nlog::println_ip(m.m_ip_address);

            ncore::nlog::printfln("  RSSI: %d dBm", va_t(m.m_rssi));
        }

    }  // namespace nnet
}  // namespace ncore

#    endif

#endif
