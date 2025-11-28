#ifdef TARGET_ARDUINO

#    include "Arduino.h"

#    include "rdno_wifi/c_ethernet.h"
#    include "WiFi.h"

#    ifdef TARGET_ESP8266
#        include "ESP8266WiFi.h"
#    endif

#    include "rdno_wifi/c_wifi.h"
#    include "rdno_core/c_eeprom.h"
#    include "rdno_core/c_network.h"
#    include "rdno_core/c_serial.h"
#    include "rdno_core/c_str.h"
#    include "ccore/c_memory.h"

namespace ncore
{
    namespace nwifi
    {
        state_wifi_t gWiFiState;

        void cache_t::reset() { g_memclr(this, sizeof(cache_t)); }

        void init_state(state_t* state, bool load_cache)
        {
            state->wifi           = &gWiFiState;
            state->wifi->m_status = nstatus::Disconnected;
            g_memset(state->wifi->m_mac, 0, sizeof(state->wifi->m_mac));

            if (load_cache)
            {
                ncore::nserial::println("loading WiFi cache from EEPROM");
                ncore::neeprom::load((byte*)&state->wifi->m_cache, sizeof(ncore::nwifi::cache_t));
                {
                    const u32 crc              = state->wifi->m_cache.m_crc;
                    state->wifi->m_cache.m_crc = 0;
                    if (crc != neeprom::crc32((const byte*)&state->wifi->m_cache, sizeof(nwifi::cache_t)))
                    {
                        ncore::nserial::println(" WiFi cache in EEPROM is corrupted (CRC mismatch)");
                        state->wifi->m_cache.reset();
                    }
                    else
                    {
                        ncore::nserial::println("WiFi cache loaded from EEPROM");
                        state->wifi->m_cache.m_crc = crc;
                    }
                }
            }
            else
            {
                state->wifi->m_cache.reset();
            }
        }

        void disconnect() { WiFi.disconnect(); }
        void disconnect_AP(bool wifioff) { WiFi.softAPdisconnect(wifioff); }

        IPAddress_t get_IP(state_t* state) { return WiFi.localIP(); }

        const u8* get_MAC(state_t* state)
        {
            WiFi.macAddress(state->MACAddress);
            return state->MACAddress;
        }

        s32 get_RSSI(state_t* state) { return WiFi.RSSI(); }
        void set_DNS(const IPAddress_t& dns) { WiFi.setDNS(dns); }

        // TODO Should we add compile time verification of the nencryption::type_t
        // matching the values from the WiFi library?

        // ----------------------------------------------------------------------------------------
        // ----------------------------------------------------------------------------------------
        // from: https://github.com/softplus/esp8266-wifi-timing

        // do a fast-connect, if we can, return true if ok
        void fast_connect_fast(const char* ssid, const char* auth, nwifi::cache_t const& cache)
        {
            WiFi.config(cache.ip_address, cache.ip_gateway, cache.ip_mask);
            WiFi.begin(ssid, auth, cache.wifi_channel, cache.wifi_bssid, true);
        }

        // do a normal wifi connection, once connected cache connection info, return true if ok
        void fast_connect_normal(const char* ssid, const char* auth) { WiFi.begin(ssid, auth); }

        // Connect to wifi as specified, returns true if ok
        void connect(state_t* state, bool force_normal_mode)
        {
            WiFi.setAutoReconnect(false);  // prevent early autoconnect
            WiFi.persistent(true);
            WiFi.mode(WIFI_STA);

            if (state->wifi->m_cache.ip_address == 0 || force_normal_mode)
            {
                nserial::printf("Connect (normal) to WiFi with SSID %s ...\n", va_t(state->WiFiSSID));
                fast_connect_normal(state->WiFiSSID, state->WiFiPassword);
            }
            else
            {
                nserial::printf("Connect (fast) to WiFi with SSID %s ...\n", va_t(state->WiFiSSID));
                fast_connect_fast(state->WiFiSSID, state->WiFiPassword, state->wifi->m_cache);
            }
        }

        bool connected(state_t* state)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                if (state->wifi->m_status != nstatus::Connected)
                {
                    state->wifi->m_status = nstatus::Connected;

                    WiFi.macAddress(state->wifi->m_mac);

                    nwifi::cache_t& cache = state->wifi->m_cache;
                    cache.ip_address      = WiFi.localIP();
                    cache.ip_gateway      = WiFi.gatewayIP();
                    cache.ip_mask         = WiFi.subnetMask();
                    cache.ip_dns1         = WiFi.dnsIP(0);
                    cache.ip_dns2         = WiFi.dnsIP(1);
                    WiFi.BSSID(cache.wifi_bssid);
                    cache.wifi_channel = WiFi.channel();
                    cache.m_crc        = 0;
                    cache.m_crc        = neeprom::crc32((const byte*)&cache, sizeof(nwifi::cache_t));
                    neeprom::save((const byte*)&cache, sizeof(nwifi::cache_t));
                }
                return true;
            }
            state->wifi->m_status = nstatus::Disconnected;
            return false;
        }

        void disconnect(state_t* state)
        {
            WiFi.disconnect();
            state->wifi->m_status = nstatus::Disconnected;
        }

        void print_connection_info(state_t* state)
        {
            ncore::nserial::println("WiFi Connection Info:");

#    ifdef TARGET_ESP8266
            // Print PhyMode
            if (WiFi.getPhyMode() == WIFI_PHY_MODE_11B)
            {
                ncore::nserial::println(" PhyMode: 802.11b");
            }
            else if (WiFi.getPhyMode() == WIFI_PHY_MODE_11G)
            {
                ncore::nserial::println(" PhyMode: 802.11g");
            }
            else if (WiFi.getPhyMode() == WIFI_PHY_MODE_11N)
            {
                ncore::nserial::println(" PhyMode: 802.11n");
            }
            else
            {
                ncore::nserial::println(" PhyMode: Unknown");
            }
#    endif

            ncore::nserial::print(" SSID: ");
            ncore::nserial::println(state->WiFiSSID);
            ncore::nserial::print(" MAC Address: ");
            const u8* mac = state->wifi->m_mac;
            ncore::nserial::print(mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            ncore::nserial::println("");
            IPAddress ip = WiFi.localIP();
            ncore::nserial::printf("  IP Address: %d.%d.%d.%d\n", va_t(ip[0]), va_t(ip[1]), va_t(ip[2]), va_t(ip[3]));
            ncore::nserial::printf(" RSSI: %d dBm\n", va_t(WiFi.RSSI()));
        }

    }  // namespace nwifi
}  // namespace ncore

#else

#    include "rdno_wifi/c_wifi.h"

namespace ncore
{
    namespace nwifi
    {
        void init_state(state_t* state) { CC_UNUSED(state); }
        void connect(state_t* state) { CC_UNUSED(state); }
        bool connected(state_t* state)
        {
            CC_UNUSED(state);
            return false;
        }
        void disconnect(state_t* state) { CC_UNUSED(state); }

        void get_IP(state_t* state, IPAddress_t& ip_address)
        {
            CC_UNUSED(state);
            CC_UNUSED(ip_address);
        }

        void get_MAC(state_t* state, MACAddress_t& mac_address)
        {
            CC_UNUSED(state);
            CC_UNUSED(mac_address);
        }

        s32 get_RSSI(state_t* state)
        {
            CC_UNUSED(state);
            return 0;
        }

        void print_connection_info(state_t* state) { CC_UNUSED(state); }

    }  // namespace nwifi
}  // namespace ncore

#endif
