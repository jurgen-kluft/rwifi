#ifdef TARGET_ARDUINO

#    include "rcore/c_app.h"
#    include "rcore/c_packet.h"
#    include "rcore/c_network.h"
#    include "rcore/c_eeprom.h"
#    include "rcore/c_log.h"
#    include "rcore/c_state.h"
#    include "rcore/c_str.h"
#    include "rcore/c_task.h"
#    include "rcore/c_timer.h"

#    include "rwifi/c_tcp.h"
#    include "rwifi/c_udp.h"
#    include "rwifi/c_wifi.h"
#    include "rwifi/c_node.h"

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
                nlog::println("  -> Connecting to remote stopped, WiFi not connected.");
                return ntask::RESULT_ERROR;
            }

            if (state->node->remote_mode == 0)
            {
                nlog::println("Connecting to remote tcp server...");
            }
            else
            {
                nlog::println("Connecting to remote udp server...");
                nudp::open(state, 31337);  // Open UDP socket on port 31337
            }

            return ntask::RESULT_DONE;
        }

        ntask::result_t func_remote_connecting(state_t* state)
        {
            if (state->node->remote_mode == 0)
            {
                IPAddress_t remote_server_ip_address = IPAddress_t::from(state->ServerIP);
                const u16   remote_port              = state->ServerTcpPort;

#    ifdef TARGET_DEBUG
                STR_T(ipStr, 32, "");
                remote_server_ip_address.to_string(ipStr);
                nlog::printfln("Connecting to %s ...", va_t(ipStr.m_const), va_t(remote_port));
#    endif
                state->node->tcp_client = ntcp::connect(state->tcp, remote_server_ip_address, remote_port, 8000);
                if (ntcp::connected(state->tcp, state->node->tcp_client))
                {
#    ifdef TARGET_DEBUG
                    nlog::println("  -> Connected to remote.");

                    IPAddress_t localIP = ntcp::local_IP(state->tcp, state->node->tcp_client);
                    nlog::print("     IP: ");
                    localIP.to_string(ipStr);
                    nlog::println(ipStr.m_const);

                    nlog::print("     MAC: ");
                    nlog::println_mac(state->MACAddress);
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
                IPAddress_t remote_server_ip = IPAddress_t::from(state->ServerIP);
                const u16   remote_port      = state->ServerUdpPort;
                nudp::send_to(state, remote_port, data, size, remote_server_ip, remote_port);
            }
        }

    }  // namespace nnode
}  // namespace ncore

#else

#    include "rwifi/c_node.h"

namespace ncore
{
    namespace nnode
    {
        void initialize(state_t* state, state_task_t* task_state) {}
        void send_sensor_data(state_t* state, const byte* data, const s32 size) {}

    }  // namespace nnode
}  // namespace ncore

#endif
