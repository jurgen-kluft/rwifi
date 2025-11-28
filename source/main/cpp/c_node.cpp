#ifdef TARGET_ARDUINO

#    include "Arduino.h"

#    include "rdno_wifi/c_ethernet.h"
#    include "WiFi.h"

#    include "rdno_core/c_app.h"
#    include "rdno_core/c_packet.h"
#    include "rdno_core/c_network.h"
#    include "rdno_core/c_eeprom.h"
#    include "rdno_core/c_serial.h"
#    include "rdno_core/c_state.h"
#    include "rdno_core/c_str.h"
#    include "rdno_core/c_task.h"
#    include "rdno_core/c_timer.h"

#    include "rdno_wifi/c_tcp.h"
#    include "rdno_wifi/c_udp.h"
#    include "rdno_wifi/c_wifi.h"
#    include "rdno_wifi/c_node.h"

namespace ncore
{
    struct state_node_t
    {
        u8             remote_mode;
        ntcp::client_t tcp_client;
    };

    namespace nnode
    {
        state_node_t gNodeState;

        // ----------------------------------------------------------------
        // ----------------------------------------------------------------
        // ntask based version
        // ----------------------------------------------------------------
        // ----------------------------------------------------------------

        ntask::result_t func_connect_to_WiFi_start(state_t* state)
        {
            nwifi::connect(state);
            return ntask::RESULT_DONE;
        }

        ntask::result_t func_wifi_is_connected(state_t* state)
        {
            if (nwifi::connected(state))
                return ntask::RESULT_DONE;
            return ntask::RESULT_OK;
        }

        ntask::result_t func_wifi_disconnect(state_t* state)
        {
            nwifi::disconnect(state);
            return ntask::RESULT_DONE;
        }

        ntask::result_t func_connect_to_remote_start(state_t* state)
        {
            if (state->node->remote_mode == 0)
            {
                ntcp::disconnect(state->tcp, state->node->tcp_client);  // Stop any existing client connection
            }

            if (nwifi::connected(state) == false)
            {
                nserial::println("  -> Connecting to remote stopped, WiFi not connected.");
                return ntask::RESULT_ERROR;
            }

            if (state->node->remote_mode == 0)
            {
                nserial::println("Connecting to remote tcp server...");
            }
            else
            {
                nserial::println("Connecting to remote udp server...");
                nudp::open(state, 31337);  // Open UDP socket on port 31337
            }

            return ntask::RESULT_DONE;
        }

        ntask::result_t func_remote_connecting(state_t* state)
        {
            if (state->node->remote_mode == 0)
            {
                IPAddress_t remote_server_ip_address(state->ServerIP);
                const u16   remote_port = state->ServerTcpPort;

#    ifdef TARGET_DEBUG
                nserial::printf("Connecting to %d.%d.%d.%d:%d ...\n", va_t(remote_server_ip_address[0]), va_t(remote_server_ip_address[1]), va_t(remote_server_ip_address[2]), va_t(remote_server_ip_address[3]), va_t(remote_port));
#    endif
                state->node->tcp_client = ntcp::connect(state->tcp, remote_server_ip_address, remote_port, 8000);
                if (ntcp::connected(state->tcp, state->node->tcp_client))
                {
#    ifdef TARGET_DEBUG
                    nserial::println("  -> Connected to remote.");

                    IPAddress_t localIP = ntcp::local_IP(state->tcp, state->node->tcp_client);
                    nserial::print("     IP: ");
                    nserial::print(localIP[0], localIP[1], localIP[2], localIP[3]);
                    nserial::println("");

                    if (state->wifi->m_mac == nullptr)
                    {
                        nserial::println("     MAC: <unknown>");
                    }
                    else
                    {
                        nserial::print("     MAC: ");
                        const u8* mac = state->wifi->m_mac;
                        nserial::print(mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                        nserial::println("");
                    }
#    endif
                    return ntask::RESULT_DONE;
                }
            }
            else
            {
                return ntask::RESULT_DONE;
            }
            return ntask::RESULT_OK;
        }

        ntask::result_t func_remote_is_connected(state_t* state)
        {
            if (state->node->remote_mode == 0)
            {
                if (ntcp::connected(state->tcp, state->node->tcp_client))
                {
                    return ntask::RESULT_DONE;
                }
            }
            else
            {
                return ntask::RESULT_DONE;
            }
            return ntask::RESULT_OK;
        }

        // -----------------------------------------------------------------------------------
        // programs
        void             node_failure(ntask::scheduler_t* scheduler, state_t* state);
        ntask::program_t program_node_failure(node_failure);

        void             node_connecting_wifi(ntask::scheduler_t* scheduler, state_t* state);
        ntask::program_t program_node_connecting_wifi(node_connecting_wifi);

        void node_connected_tcp_program(ntask::scheduler_t* scheduler, state_t* state)
        {
            if (ntask::call(scheduler, func_wifi_is_connected))
            {
                if (ntask::call(scheduler, func_remote_is_connected))
                {
                    ntask::call_program(scheduler, scheduler->m_state_task->m_main_program);
                    return;
                }
            }
            ntask::jmp_program(scheduler, &program_node_connecting_wifi);
        }
        ntask::program_t program_node_connected_tcp(node_connected_tcp_program);

        void node_connected_udp_program(ntask::scheduler_t* scheduler, state_t* state)
        {
            if (ntask::call(scheduler, func_wifi_is_connected))
            {
                ntask::call_program(scheduler, scheduler->m_state_task->m_main_program);
                return;
            }
            ntask::jmp_program(scheduler, &program_node_connecting_wifi);
        }
        ntask::program_t program_node_connected_udp(node_connected_udp_program);

        ntask::timeout_t gRemoteConnectTimeout(300 * 1000);
        void             node_connecting_remote(ntask::scheduler_t* scheduler, state_t* state)
        {
            if (ntask::is_first_call(scheduler))
            {
                ntask::init_timeout(scheduler, gRemoteConnectTimeout);
                ntask::call(scheduler, func_connect_to_remote_start);
            }

            ntask::call(scheduler, func_remote_connecting);
            if (ntask::call(scheduler, func_remote_is_connected))
            {
                if (state->node->remote_mode == 0)
                    ntask::jmp_program(scheduler, &program_node_connected_tcp);
                else
                    ntask::jmp_program(scheduler, &program_node_connected_udp);
            }
            else if (ntask::timeout(scheduler, gRemoteConnectTimeout))
            {
                ntask::call(scheduler, func_wifi_disconnect);
                ntask::jmp_program(scheduler, &program_node_failure);
            }
        }
        ntask::program_t program_node_connecting_remote(node_connecting_remote);

        void node_failure(ntask::scheduler_t* scheduler, state_t* state)
        {
            // reset the eeprom data so that next time we try to connect again from scratch
            state->wifi->m_cache.reset();
            ntask::jmp_program(scheduler, &program_node_connecting_wifi);
        }

        ntask::timeout_t gWiFiConnectTimeout(300 * 1000);
        void             node_connecting_wifi(ntask::scheduler_t* scheduler, state_t* state)
        {
            if (ntask::is_first_call(scheduler))
            {
                ntask::init_timeout(scheduler, gWiFiConnectTimeout);
                ntask::call(scheduler, func_connect_to_WiFi_start);
            }

            if (ntask::call(scheduler, func_wifi_is_connected))
            {
                ntask::jmp_program(scheduler, &program_node_connecting_remote);
            }
            else if (ntask::timeout(scheduler, gWiFiConnectTimeout))
            {
                ntask::call(scheduler, func_wifi_disconnect);
                ntask::jmp_program(scheduler, &program_node_failure);
            }
        }

        void initialize(state_t* state, state_task_t* task_state)
        {
            nwifi::init_state(state, true);
            ntcp::init_state(state);
            nudp::init_state(state);

            state->node = &gNodeState;

            gNodeState.remote_mode = 0;  // 0 = TCP, 1 = UDP
            gNodeState.tcp_client  = nullptr;

            ntask::set_start(state, task_state, &program_node_connecting_wifi);
        }

        void send_sensor_data(state_t* state, const byte* data, const s32 size)
        {
            if (state->node->remote_mode == 0)
            {
                if (ntcp::connected(state->tcp, state->node->tcp_client))
                {
                    ntcp::write(state->tcp, state->node->tcp_client, data, size);
                }
            }
            else
            {
                IPAddress_t remote_server_ip(state->ServerIP);
                const u16   remote_port = state->ServerUdpPort;
                nudp::send_to(state, 31337, data, size, remote_server_ip, remote_port);
            }
        }

    }  // namespace nnode
}  // namespace ncore

#else

#    include "rdno_wifi/c_node.h"

namespace ncore
{
    namespace nnode
    {
        void initialize(state_t* state, state_task_t* task_state) {}
        void send_sensor_data(state_t* state, const byte* data, const s32 size) {}

    }  // namespace nnode
}  // namespace ncore

#endif
