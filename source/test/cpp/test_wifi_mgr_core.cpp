#include "ccore/c_random.h"
#include "rcore/c_eeprom.h"
#include "rcore/c_timer.h"
#include "rwifi/c_wifi_mgr_core.h"

#include "cunittest/cunittest.h"

using namespace ncore;

namespace
{
    struct fake_wifi_t
    {
        nnet::wifi_link_info_t       m_link_info;
        nnet::wifi_cache_t           m_cache;
        nnet::wifi_connect_request_t m_last_request;
        nnet::wifi_event_t           m_events[4];
        u64                          m_millis;
        u32                          m_event_read;
        u32                          m_event_write;
        u32                          m_event_count;
        u32                          m_connect_count;
        bool                         m_accept_connect;
        bool                         m_has_cache;
        bool                         m_has_request;
    };

    static u64 fake_millis(void* context) { return ((fake_wifi_t*)context)->m_millis; }
    static u64 fake_micros(void* context) { return ((fake_wifi_t*)context)->m_millis * 1000; }
    static bool fake_init(void*) { return true; }
    static void fake_shutdown(void*) {}
    static bool fake_start(void*) { return true; }
    static void fake_stop(void*) {}
    static void fake_disconnect(void*) {}

    static bool fake_connect(void* context, const nnet::wifi_connect_request_t* request)
    {
        fake_wifi_t* fake = (fake_wifi_t*)context;
        fake->m_last_request = *request;
        fake->m_has_request = true;
        fake->m_connect_count++;
        return fake->m_accept_connect;
    }

    static bool fake_poll_event(void* context, nnet::wifi_event_t* event)
    {
        fake_wifi_t* fake = (fake_wifi_t*)context;
        if (fake->m_event_count == 0)
            return false;

        *event = fake->m_events[fake->m_event_read];
        fake->m_event_read = (fake->m_event_read + 1) % 4;
        fake->m_event_count--;
        return true;
    }

    static bool fake_get_link_info(void* context, nnet::wifi_link_info_t* info)
    {
        *info = ((fake_wifi_t*)context)->m_link_info;
        return true;
    }

    static bool fake_get_mac_address(void*, u8*) { return false; }

    static bool fake_load_cache(void* context, nnet::wifi_cache_t* cache)
    {
        fake_wifi_t* fake = (fake_wifi_t*)context;
        if (!fake->m_has_cache)
            return false;

        *cache = fake->m_cache;
        return true;
    }

    static void fake_save_cache(void* context, const nnet::wifi_cache_t* cache)
    {
        fake_wifi_t* fake = (fake_wifi_t*)context;
        fake->m_cache = *cache;
        fake->m_has_cache = true;
    }

    static nnet::wifi_platform_ops_t make_platform(fake_wifi_t* fake)
    {
        nnet::wifi_platform_ops_t platform = {};
        platform.m_context = fake;
        platform.m_init = fake_init;
        platform.m_shutdown = fake_shutdown;
        platform.m_start = fake_start;
        platform.m_stop = fake_stop;
        platform.m_connect = fake_connect;
        platform.m_disconnect = fake_disconnect;
        platform.m_poll_event = fake_poll_event;
        platform.m_get_link_info = fake_get_link_info;
        platform.m_get_mac_address = fake_get_mac_address;
        platform.m_load_cache = fake_load_cache;
        platform.m_save_cache = fake_save_cache;
        return platform;
    }

    static nnet::wifi_manager_config_t make_config(random_t* random)
    {
        nnet::wifi_manager_config_t config = {};
        config.ssid = "test-network";
        config.password = "test-password";
        config.init_backoff_ms = 100;
        config.max_backoff_ms = 400;
        config.backoff_multiplier = 2.0f;
        config.jitter_percentage = 0.0f;
        config.random = random;
        return config;
    }

    static void queue_event(fake_wifi_t* fake, nnet::wifi_event_type_t type)
    {
        fake->m_events[fake->m_event_write].m_type = type;
        fake->m_event_write = (fake->m_event_write + 1) % 4;
        fake->m_event_count++;
    }

    static void finalize_cache(nnet::wifi_cache_t* cache)
    {
        cache->m_crc = 0;
        cache->m_crc = neeprom::crc32((const byte*)cache, sizeof(nnet::wifi_cache_t));
    }
}

UNITTEST_SUITE_BEGIN(wifi_mgr_core)
{
    UNITTEST_FIXTURE(state_machine)
    {
        UNITTEST_FIXTURE_SETUP() {}
        UNITTEST_FIXTURE_TEARDOWN() { ntimer::default_clock_ops(); }

        UNITTEST_TEST(normal_connection)
        {
            fake_wifi_t fake = {};
            fake.m_accept_connect = true;
            fake.m_link_info.m_cache.ip_address = 0x0102A8C0;
            fake.m_link_info.m_rssi = -42;
            xor_random_t random;
            nnet::wifi_platform_ops_t platform = make_platform(&fake);
            nnet::wifi_manager_config_t config = make_config(&random);
            ntimer::set_clock_ops(&fake, fake_millis, fake_micros);
            nnet::wifi_manager_core_t* manager = nnet::create_wifi_manager_core(&config, &platform);

            nnet::activate_wifi_manager_core(manager);
            nnet::tick_wifi_manager_core(manager);
            CHECK_TRUE(fake.m_has_request);
            CHECK_FALSE(fake.m_last_request.m_fast_connect);

            queue_event(&fake, nnet::WIFI_EVENT_GOT_IP);
            nnet::tick_wifi_manager_core(manager);
            CHECK_TRUE(nnet::is_wifi_manager_core_connected(manager));
            CHECK_EQUAL(nnet::get_wifi_manager_core_link_state(manager).m_connection_generation, 1);
            CHECK_EQUAL(nnet::get_wifi_manager_core_rssi(manager), -42);
            CHECK_EQUAL(nnet::get_wifi_manager_core_ip_address(manager)[0], 0xC0);

            nnet::destroy_wifi_manager_core(manager);
        }

        UNITTEST_TEST(fast_connect_fallback)
        {
            fake_wifi_t fake = {};
            fake.m_accept_connect = true;
            fake.m_cache.ip_address = 0x0102A8C0;
            finalize_cache(&fake.m_cache);
            fake.m_has_cache = true;
            xor_random_t random;
            nnet::wifi_platform_ops_t platform = make_platform(&fake);
            nnet::wifi_manager_config_t config = make_config(&random);
            ntimer::set_clock_ops(&fake, fake_millis, fake_micros);
            nnet::wifi_manager_core_t* manager = nnet::create_wifi_manager_core(&config, &platform);

            nnet::activate_wifi_manager_core(manager);
            nnet::tick_wifi_manager_core(manager);
            CHECK_TRUE(fake.m_last_request.m_fast_connect);

            queue_event(&fake, nnet::WIFI_EVENT_DISCONNECTED);
            nnet::tick_wifi_manager_core(manager);
            CHECK_EQUAL(fake.m_connect_count, 2);
            CHECK_FALSE(fake.m_last_request.m_fast_connect);

            nnet::destroy_wifi_manager_core(manager);
        }

        UNITTEST_TEST(connection_generation_increments_after_reconnect)
        {
            fake_wifi_t fake = {};
            fake.m_accept_connect = true;
            xor_random_t random;
            nnet::wifi_platform_ops_t platform = make_platform(&fake);
            nnet::wifi_manager_config_t config = make_config(&random);
            ntimer::set_clock_ops(&fake, fake_millis, fake_micros);
            nnet::wifi_manager_core_t* manager = nnet::create_wifi_manager_core(&config, &platform);

            nnet::activate_wifi_manager_core(manager);
            nnet::tick_wifi_manager_core(manager);
            queue_event(&fake, nnet::WIFI_EVENT_GOT_IP);
            nnet::tick_wifi_manager_core(manager);
            CHECK_EQUAL(nnet::get_wifi_manager_core_link_state(manager).m_connection_generation, 1);

            queue_event(&fake, nnet::WIFI_EVENT_DISCONNECTED);
            nnet::tick_wifi_manager_core(manager);
            fake.m_millis = 200;
            nnet::tick_wifi_manager_core(manager);
            nnet::tick_wifi_manager_core(manager);
            queue_event(&fake, nnet::WIFI_EVENT_GOT_IP);
            nnet::tick_wifi_manager_core(manager);
            CHECK_EQUAL(nnet::get_wifi_manager_core_link_state(manager).m_connection_generation, 2);

            nnet::destroy_wifi_manager_core(manager);
        }

        UNITTEST_TEST(rejected_connection_backoff)
        {
            fake_wifi_t fake = {};
            fake.m_accept_connect = false;
            xor_random_t random;
            nnet::wifi_platform_ops_t platform = make_platform(&fake);
            nnet::wifi_manager_config_t config = make_config(&random);
            ntimer::set_clock_ops(&fake, fake_millis, fake_micros);
            nnet::wifi_manager_core_t* manager = nnet::create_wifi_manager_core(&config, &platform);

            nnet::activate_wifi_manager_core(manager);
            nnet::tick_wifi_manager_core(manager);
            CHECK_EQUAL(fake.m_connect_count, 1);

            fake.m_millis = 199;
            nnet::tick_wifi_manager_core(manager);
            CHECK_EQUAL(fake.m_connect_count, 1);

            fake.m_millis = 200;
            nnet::tick_wifi_manager_core(manager);
            nnet::tick_wifi_manager_core(manager);
            CHECK_EQUAL(fake.m_connect_count, 2);

            nnet::destroy_wifi_manager_core(manager);
        }
    }
}
UNITTEST_SUITE_END
