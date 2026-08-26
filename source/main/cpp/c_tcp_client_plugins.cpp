#ifdef TARGET_ARDUINO

#    include "Arduino.h"

// #    include "rwifi/c_ethernet.h"
#    include "WiFi.h"

#    ifdef TARGET_ESP8266
#        include "ESP8266WiFi.h"
#    endif

#endif

#include "rcore/c_eeprom.h"
#include "rcore/c_network.h"
#include "rcore/c_log.h"
#include "rcore/c_str.h"
#include "rcore/c_system.h"

#include "ccore/c_memory.h"

#include "rwifi/c_wifi.h"
#include "rwifi/c_tcp_client.h"

namespace ncore
{
    namespace nnet
    {
        enum msg_types_t
        {
            MSG_TYPE_HANDSHAKE      = 0x01,
            MSG_TYPE_HANDSHAKE_ACK  = 0x02,
            MSG_TYPE_DATA_INIT      = 0x03,
            MSG_TYPE_DATA_INIT_ACK  = 0x04,
            MSG_TYPE_DATA_BLOCK     = 0x05,
            MSG_TYPE_DATA_BLOCK_ACK = 0x06
        };

        // 888    888        d8888 888b    888 8888888b.   .d8888b.  888    888        d8888 888    d8P  8888888888
        // 888    888       d88888 8888b   888 888  "Y88b d88P  Y88b 888    888       d88888 888   d8P   888
        // 888    888      d88P888 88888b  888 888    888 Y88b.      888    888      d88P888 888  d8P    888
        // 8888888888     d88P 888 888Y88b 888 888    888  "Y888b.   8888888888     d88P 888 888d88K     8888888
        // 888    888    d88P  888 888 Y88b888 888    888     "Y88b. 888    888    d88P  888 8888888b    888
        // 888    888   d88P   888 888  Y88888 888    888       "888 888    888   d88P   888 888  Y88b   888
        // 888    888  d8888888888 888   Y8888 888  .d88P Y88b  d88P 888    888  d8888888888 888   Y88b  888
        // 888    888 d88P     888 888    Y888 8888888P"   "Y8888P"  888    888 d88P     888 888    Y88b 8888888888

        // Handshake mechanism:
        // 1. ESP32 connects to the Mac over TCP, the Mac sends a handshake message (MSG_TYPE_HANDSHAKE_INITIATE) to the ESP32.
        // 2. The ESP32 receives the handshake message, processes it, and sends back a handshake acknowledgment 
        //    (MSG_TYPE_HANDSHAKE_ACK) to the Mac with its public key and other information.
        // 3. The Mac receives the handshake acknowledgment, processes it, and sends back a handshake final acknowledgment 
        //    (MSG_TYPE_HANDSHAKE_FINAL_ACK) to the ESP32.

        #define MSG_TYPE_HANDSHAKE_INITIATE 0x01
        #define MSG_TYPE_HANDSHAKE_ACK 0x02
        #define MSG_TYPE_HANDSHAKE_FINAL_ACK 0x03

        // --- MSG TYPE 0x01: Handshake Initiate (Mac -> ESP32) ---
        struct handshake_initiate_t : public msg_hdr_t
        {
        };

        // --- MSG TYPE 0x02: Handshake Ack (ESP32 -> Mac) ---
        struct handshake_ack_t : public msg_hdr_t
        {
            u8 mac_address[6];
            u8 reserved0[58];
        };

        // --- MSG TYPE 0x03: Handshake Final Ack (Mac -> ESP32) ---
        struct handshake_final_ack_t : public msg_hdr_t
        {
            u32 status;  // 0x01 = Approved, 0x00 = Denied
        };

        // hand-shake plugin: handles the initial handshake with the Mac, including public key exchange and authentication
        static byte s_handshake_buffer[8];
        bool        handshake_acquire_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* hdr, tcp_buffer_t* out)
        {
            if (hdr->msg_type != MSG_TYPE_HANDSHAKE_INITIATE)
                return false;  // Not a handshake initiate message

            ASSERT(hdr->payload_len == 0);  // Handshake initiate has no payload

            out->m_buffer = s_handshake_buffer;
            out->m_length = 0; 

            return true;  // Handled handshake ack message
        }

        void handshake_commit_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* hdr, tcp_buffer_t buffer)
        {
            // Call the user-defined callback to indicate handshake completion
            // The user should process the handshake message, e.g., verify public key, authenticate, etc.
            // Then when everything is Ok, it should send back a handshake acknowledgment to the Remote.
            if (hdr->msg_type != MSG_TYPE_HANDSHAKE_INITIATE)
                return;  // Not a handshake initiate message

            if (hdr->msg_type == MSG_TYPE_HANDSHAKE_INITIATE)
            {
                // Prepare the handshake ack message
                handshake_ack_t ack_msg;
                ack_msg.magic       = 0xF00D;
                ack_msg.msg_type    = MSG_TYPE_HANDSHAKE_ACK;
                ack_msg.payload_len = sizeof(handshake_ack_t) - sizeof(msg_hdr_t);
                ack_msg.checksum    = 0;  // No checksum 

                // Fill in the MAC address (for example, using a placeholder here)
                u8 mac[6];
                g_memcpy(ack_msg.mac_address, mac, sizeof(mac));

                // Send the handshake ack message back to the Mac
                nnet::send_later(plugin->m_client, (byte*)&ack_msg, sizeof(handshake_ack_t));
            }
            else if (hdr->msg_type == MSG_TYPE_HANDSHAKE_FINAL_ACK)
            {
                // Process the final ack message from the Mac
                handshake_final_ack_t* final_ack = (handshake_final_ack_t*)buffer.m_buffer;
                if (plugin->m_on_complete)
                {
                    const u32 success = (final_ack->status == 0x01) ? 1 : 0;
                    plugin->m_on_complete(plugin->m_on_complete_ctx, success, 0, nullptr);
                }
            }
        }

        void handshake_abort_fn(tcp_recv_plugin_t* plugin)
        {
            // Handle any cleanup or state reset if the handshake is aborted
        }

        tcp_recv_plugin_t* new_handshake_plugin(tcp_recv_complete_fn on_complete, void* on_complete_ctx)
        {
            tcp_recv_plugin_t* plugin = new tcp_recv_plugin_t();
            plugin->m_plugin_data     = nullptr;
            plugin->m_acquire         = handshake_acquire_fn;
            plugin->m_commit          = handshake_commit_fn;
            plugin->m_abort           = handshake_abort_fn;
            plugin->m_on_complete_ctx = on_complete_ctx;
            plugin->m_on_complete     = on_complete;

            return plugin;
        }

        // 8888888b.   .d88888b.  888       888 888b    888 888      .d88888b.        d8888 8888888b.
        // 888  "Y88b d88P" "Y88b 888   o   888 8888b   888 888     d88P" "Y88b      d88888 888  "Y88b
        // 888    888 888     888 888  d8b  888 88888b  888 888     888     888     d88P888 888    888
        // 888    888 888     888 888 d888b 888 888Y88b 888 888     888     888    d88P 888 888    888
        // 888    888 888     888 888d88888b888 888 Y88b888 888     888     888   d88P  888 888    888
        // 888    888 888     888 88888P Y88888 888  Y88888 888     888     888  d88P   888 888    888
        // 888  .d88P Y88b. .d88P 8888P   Y8888 888   Y8888 888     Y88b. .d88P d8888888888 888  .d88P
        // 8888888P"   "Y88888P"  888P     Y888 888    Y888 88888888 "Y88888P" d88P     888 8888888P"

        // --- MSG TYPE 0x10: Data Init (ESP32 -> Mac) ---
        struct payload_data_init_t
        {
            u32 data_type;     // Custom
            u32 total_blocks;  // Total number of blocks that are to be sent (max 65535 blocks)
            u32 block_size;    // Size of each block (max 8192 bytes)
            u32 total_size;    // Up to 4GB
        };

        // --- MSG TYPE 0x11: Data Block Chunk (Mac -> ESP32) ---
        struct data_block_header_t
        {
            u16 block_index;  // 0-indexed block counter (max 65535 blocks)
            u16 block_size;   // Size of the following data chunk (max 8192 bytes)
            u32 file_offset;  // Absolute byte offset in file
            // u8 data[block_size] follows directly in the stream
        };

        struct data_init_ack_t
        {
            u32 status;  // 0x01 = Ready to receive, 0x00 = Out of memory/Error
        };

        struct data_block_ack_t
        {
            u32 block_index;  // Confirms receipt of specific block
            u32 status;       // 0x01 = Success, 0x00 = Corrupt/Retry
        };

        struct download_plugin_data_t
        {
            byte* m_recv_buffer;         // PSRAM buffer for receiving blocks of data
            byte* m_target_buffer;       // PSRAM destination for the full downloaded data
            u32   m_target_buffer_size;  // Size of the PSRAM buffer
            u32   m_total_blocks;        // Total number of blocks expected to be received
            u32   m_received_blocks;     // Number of blocks received so far
            u32   m_data_type;           // Remember data type
        };

        bool download_acquire_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* in_hdr, tcp_buffer_t* out_buffer)
        {
            if (in_hdr->msg_type != 0x10 && in_hdr->msg_type != 0x11)
                return false;  // Not a download message

            if (in_hdr->msg_type == 0x10)
            {
                ASSERT(in_hdr->payload_len == sizeof(payload_data_init_t));
                payload_data_init_t*    init_payload  = (payload_data_init_t*)plugin->m_plugin_data;
                download_plugin_data_t* download_data = (download_plugin_data_t*)plugin->m_plugin_data;

                // Allocate buffer for receiving blocks
                download_data->m_recv_buffer = (byte*)nsystem::malloc(init_payload->block_size + sizeof(data_block_header_t));

                out_buffer->m_buffer = download_data->m_recv_buffer;
                out_buffer->m_length = in_hdr->payload_len;  // Should be sizeof(payload_download_t)
            }
            else if (in_hdr->msg_type == 0x11)
            {
                ASSERT(in_hdr->payload_len >= sizeof(data_block_header_t));
                download_plugin_data_t* download_data = (download_plugin_data_t*)plugin->m_plugin_data;
                out_buffer->m_buffer                  = download_data->m_recv_buffer;
                out_buffer->m_length                  = in_hdr->payload_len;  // Should be sizeof(payload_download_t)
            }

            return true;  // Handled download message
        }

        void download_commit_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* hdr, tcp_buffer_t buffer)
        {
            if (hdr->msg_type == 0x10)
            {
                payload_data_init_t*    init_payload  = (payload_data_init_t*)buffer.m_buffer;
                download_plugin_data_t* download_data = (download_plugin_data_t*)plugin->m_plugin_data;
                download_data->m_data_type            = init_payload->data_type;
                download_data->m_target_buffer_size   = init_payload->total_size;
                // PSRAM allocation
                const u32 alignment            = 32;  // Align to 32 bytes for better performance
                download_data->m_target_buffer = (byte*)nsystem::alloc_psram_aligned(download_data->m_target_buffer_size, alignment);
            }
            else if (hdr->msg_type == 0x11)
            {
                data_block_header_t*    block_header  = (data_block_header_t*)buffer.m_buffer;
                const byte*             data          = buffer.m_buffer + sizeof(data_block_header_t);
                download_plugin_data_t* download_data = (download_plugin_data_t*)plugin->m_plugin_data;

                // Copy the received block into the target buffer at the specified offset
                if (download_data->m_target_buffer && block_header->file_offset + block_header->block_size <= download_data->m_target_buffer_size)
                {
                    g_memcpy(download_data->m_target_buffer + block_header->file_offset, data, block_header->block_size);
                    download_data->m_received_blocks++;
                }

                // Was this the last block? If so, call the on_complete callback
                if (download_data->m_received_blocks == download_data->m_total_blocks)
                {
                    if (plugin->m_on_complete)
                    {
                        plugin->m_on_complete(plugin->m_on_complete_ctx, download_data->m_data_type, download_data->m_target_buffer_size, download_data->m_target_buffer);

                        // We are done downloading, so we can free the receive buffer.
                        nsystem::free(download_data->m_recv_buffer);
                        download_data->m_recv_buffer = nullptr;

                        // The target buffer is now owned by the user and should be freed by them when done.
                        download_data->m_target_buffer      = nullptr;
                        download_data->m_target_buffer_size = 0;
                    }
                }
            }
        }

        void download_abort_fn(tcp_recv_plugin_t* plugin)
        {
            // Handle any cleanup or state reset if the handshake is aborted
            download_plugin_data_t* download_data = (download_plugin_data_t*)plugin->m_plugin_data;
            if (download_data->m_recv_buffer)
            {
                nsystem::free(download_data->m_recv_buffer);
                download_data->m_recv_buffer = nullptr;
            }
            if (download_data->m_target_buffer)
            {
                nsystem::free(download_data->m_target_buffer);
                download_data->m_target_buffer      = nullptr;
                download_data->m_target_buffer_size = 0;
            }
        }

        // There can only be one download plugin active at a time
        static download_plugin_data_t s_download_plugin_data;

        tcp_recv_plugin_t* new_download_plugin(tcp_recv_complete_fn on_complete, void* on_complete_ctx)
        {
            download_plugin_data_t* data = &s_download_plugin_data;
            data->m_recv_buffer          = nullptr;  // Buffer for receiving blocks of data
            data->m_target_buffer        = nullptr;  // Destination will be set when receiving the first block
            data->m_target_buffer_size   = 0;
            data->m_total_blocks         = 0;
            data->m_received_blocks      = 0;

            tcp_recv_plugin_t* plugin = new tcp_recv_plugin_t();
            plugin->m_plugin_data     = data;
            plugin->m_acquire         = download_acquire_fn;
            plugin->m_commit          = download_commit_fn;
            plugin->m_abort           = download_abort_fn;
            plugin->m_on_complete_ctx = on_complete_ctx;
            plugin->m_on_complete     = on_complete;
            return plugin;
        }

    }  // namespace nnet
}  // namespace ncore
