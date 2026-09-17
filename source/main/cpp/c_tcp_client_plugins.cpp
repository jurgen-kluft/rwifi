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
            MSG_TYPE_HANDSHAKE            = 0x01,
            MSG_TYPE_HANDSHAKE_ACK        = 0x02,
            MSG_TYPE_HANDSHAKE_ACK_ACK    = 0x03,
            MSG_TYPE_DATA_BLOCK_INIT      = 0x10,
            MSG_TYPE_DATA_BLOCK_CHUNK     = 0x11,
            MSG_TYPE_DATA_BLOCK_INIT_ACK  = 0x12,
            MSG_TYPE_DATA_BLOCK_CHUNK_ACK = 0x13,
            MSG_TYPE_HOUSE_META_DATA      = 0x20,
            MSG_TYPE_HOUSE_DATA           = 0x21,
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
        // 1. ESP32 connects to the Mac over TCP, the Mac sends a handshake message (MSG_TYPE_HANDSHAKE) to the ESP32
        //    containing pairs of [asset type, asset version] that the Mac has available for download.
        // 2. The ESP32 receives the handshake message, processes it, and sends back a handshake acknowledgment
        //    (MSG_TYPE_HANDSHAKE_ACK) to the Mac with its own handshake response, which includes the asset types and
        //    versions that the ESP32 is interested in downloading.
        // 3. The Mac receives the handshake acknowledgment, processes it, and sends back a handshake final acknowledgment
        //    (MSG_TYPE_HANDSHAKE_ACK_ACK) to the ESP32.

        // Handshake message layout
        // - msg header
        // - payload

        // --- MSG TYPE 0x01: Handshake Initiate (Mac -> ESP32) ---
        struct handshake_initiate_t : public msg_hdr_t
        {
        };

        // --- MSG TYPE 0x02: Handshake Ack (ESP32 -> Mac) ---
        struct handshake_ack_t : public msg_hdr_t
        {
        };

        // --- MSG TYPE 0x03: Handshake Final Ack (Mac -> ESP32) ---
        struct handshake_final_ack_t : public msg_hdr_t
        {
            u32 status;  // 0x01 = Approved, 0x00 = Denied
        };

        // hand-shake plugin: handles the initial handshake with the Mac, including public key exchange and authentication

        struct handshake_plugin_data_t
        {
            const byte*               m_handshake_payload;
            u32                       m_handshake_payload_size;
            void*                     m_user_ctx;
            tcp_recv_user_complete_fn m_on_user_complete;
            byte                      m_target_buffer[64];   // destination for the payload
            u32                       m_target_buffer_size;  // size of the target buffer
            u32                       m_data_type;           // Remember data type
        };

        i32 handshake_acquire_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* hdr, buffer_t* out)
        {
            if (hdr->msg_type != MSG_TYPE_HANDSHAKE)
                return -1;  // Not a handshake initiate message

            handshake_plugin_data_t* plugin_data = (handshake_plugin_data_t*)plugin->m_plugin_data;

            // The user will have to provide us with a buffer to receive the handshake payload
            plugin_data->m_data_type = hdr->msg_type;

            out->m_buffer = plugin_data->m_target_buffer;
            out->m_length = plugin_data->m_target_buffer_size;

            return 0;  // Handled handshake ack message
        }

        void handshake_commit_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* hdr, buffer_t buffer)
        {
            if (hdr->msg_type != MSG_TYPE_HANDSHAKE)
                return;  // Not a handshake initiate message

            handshake_plugin_data_t* plugin_data = (handshake_plugin_data_t*)plugin->m_plugin_data;

            if (hdr->msg_type == MSG_TYPE_HANDSHAKE)
            {
                // Process the handshake initiate message from the Mac, it will contain pairs
                // of [asset type, asset version]

                // Call the on_begin callback to notify the user that a new handshake initiate message has been received
                plugin_data->m_target_buffer_size = 0;

                    // The user will verify the handshake message, e.g., check the asset type and version
                    // It will also modify the target buffer with its own handshake response
                    plugin_data->m_target_buffer_size = buffer.m_length;
                    plugin_data->m_data_type          = hdr->msg_type;
                    plugin_data->m_on_user_acquire(plugin->m_user_ctx, hdr->msg_type, plugin_data->m_target_buffer_size, plugin_data->m_target_buffer);

                // Prepare the handshake ack message
                byte            ack_msg_memory[sizeof(handshake_ack_t) + (8 * (4 + 4))];
                handshake_ack_t ack_msg = (*(handshake_ack_t*)ack_msg_memory);
                ack_msg.Magic           = 0xF00D;
                ack_msg.Type            = MSG_TYPE_HANDSHAKE_ACK;
                ack_msg.PayloadSize     = plugin_data->m_target_buffer_size;
                ack_msg.Checksum        = 0;  // No checksum
                const u8* mac           = get_mac_address(plugin->m_wifi_mgr);
                g_memcpy(ack_msg.Mac, mac, 6);
                g_memcpy(ack_msg_memory + sizeof(handshake_ack_t), plugin_data->m_target_buffer, plugin_data->m_target_buffer_size);

                // Send the handshake ack message back to the Mac
                nnet::send_later(plugin->m_client, ack_msg_memory, sizeof(handshake_ack_t) + plugin_data->m_target_buffer_size);
            }
            else if (hdr->msg_type == MSG_TYPE_HANDSHAKE_ACK_ACK)
            {
                // Process the final ack message from the Mac
                handshake_final_ack_t* final_ack = (handshake_final_ack_t*)buffer.m_buffer;
                if (plugin->m_on_user_complete)
                {
                    const u32 success = (final_ack->status == 0x01) ? 1 : 0;
                    plugin->m_on_user_complete(plugin->m_on_complete_ctx, success, 0, nullptr);
                }
            }
        }

        void handshake_abort_fn(tcp_recv_plugin_t* plugin)
        {
            // Handle any cleanup or state reset if the handshake is aborted
        }

        // Note: The on_begin callback is called when the plugin begins processing a new message and is there for the user to
        //       provide additional payload information.

        tcp_recv_plugin_t* new_handshake_plugin(const byte* handshake_payload, u32 handshake_payload_size, tcp_recv_user_complete_fn on_complete, void* user_context)
        {
            tcp_recv_plugin_t* plugin = nsystem::calloc(sizeof(tcp_recv_plugin_t));
            plugin->m_plugin_data     = nullptr;
            plugin->m_acquire         = handshake_acquire_fn;
            plugin->m_commit          = handshake_commit_fn;
            plugin->m_abort           = handshake_abort_fn;

            handshake_plugin_data_t* plugin_data = nsystem::calloc(sizeof(handshake_plugin_data_t));
            plugin_data->m_on_user_acquire       = on_handshake;
            plugin_data->m_on_complete_ctx       = on_complete_ctx;
            plugin_data->m_on_user_complete      = on_complete;
            plugin->m_plugin_data                = plugin_data;

            return plugin;
        }

        void destroy_handshake_plugin(tcp_recv_plugin_t* plugin) { nsystem::free(plugin); }

        // 8888888b.   .d88888b.  888       888 888b    888 888      .d88888b.        d8888 8888888b.
        // 888  "Y88b d88P" "Y88b 888   o   888 8888b   888 888     d88P" "Y88b      d88888 888  "Y88b
        // 888    888 888     888 888  d8b  888 88888b  888 888     888     888     d88P888 888    888
        // 888    888 888     888 888 d888b 888 888Y88b 888 888     888     888    d88P 888 888    888
        // 888    888 888     888 888d88888b888 888 Y88b888 888     888     888   d88P  888 888    888
        // 888    888 888     888 88888P Y88888 888  Y88888 888     888     888  d88P   888 888    888
        // 888  .d88P Y88b. .d88P 8888P   Y8888 888   Y8888 888     Y88b. .d88P d8888888888 888  .d88P
        // 8888888P"   "Y88888P"  888P     Y888 888    Y888 88888888 "Y88888P" d88P     888 8888888P"

        // NOTE:
        //
        // Downloading data is done in pieces called "blocks". Each block is sent from the Mac to the ESP32 in a separate message.
        // The ESP32 will acknowledge each block received, and the Mac will resend any block that is marked as corrupt or not acknowledged.
        // The ESP32 will reassemble the blocks into a complete data set in PSRAM, and then call the on_complete callback when all blocks
        // have been received and verified.

        // --- MSG TYPE 0x10: Data Init (Mac -> ESP32) ---
        struct download_init_t
        {
            u32 data_type;     // Custom
            u32 total_blocks;  // Total number of blocks that are to be sent (max 65535 blocks)
            u32 block_size;    // Size of each block (max 8192 bytes)
            u32 total_size;    // Up to 4GB
        };

        // --- MSG TYPE 0x11: Data Block Chunk (Mac -> ESP32) ---
        struct download_block_header_t
        {
            u16 block_index;  // 0-indexed block counter (max 65535 blocks)
            u16 block_size;   // Size of the following data chunk (max 8192 bytes)
            u32 file_offset;  // Absolute byte offset in file
            // u8 data[block_size] follows directly in the stream
        };

        struct download_init_ack_t : public msg_hdr_t
        {
            u32 status;  // 0x01 = Ready to receive, 0x00 = Out of memory/Error
        };

        struct download_block_ack_t : public msg_hdr_t
        {
            u32 block_index;  // Confirms receipt of specific block
            u32 status;       // 0x01 = Success, 0x00 = Corrupt/Retry
        };

        struct download_plugin_data_t
        {
            void*                     m_user_ctx;
            tcp_recv_user_acquire_fn  m_on_user_acquire;
            tcp_recv_user_complete_fn m_on_user_complete;
            byte*                     m_target_buffer;       // PSRAM destination for the full downloaded data
            u32                       m_target_buffer_size;  // Size of the PSRAM buffer
            u32                       m_total_blocks;        // Total number of blocks expected to be received
            u32                       m_received_blocks;     // Number of blocks received so far
            u32                       m_data_type;           // Remember data type
        };

        bool download_acquire_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* in_hdr, buffer_t* out_buffer)
        {
            if (in_hdr->msg_type != MSG_TYPE_DATA_BLOCK_INIT && in_hdr->msg_type != MSG_TYPE_DATA_BLOCK_CHUNK)
                return false;  // Not a download message

            download_plugin_data_t* plugin_data = (download_plugin_data_t*)plugin->m_plugin_data;

            if (in_hdr->msg_type == MSG_TYPE_DATA_BLOCK_INIT)
            {
                ASSERT(in_hdr->payload_len == sizeof(download_init_t));
                download_init_t* init_payload = (download_init_t*)plugin->m_plugin_data;

                // Allocate buffer for receiving blocks
                // plugin_data->m_recv_buffer = (byte*)nsystem::malloc(init_payload->block_size + sizeof(download_block_header_t));

                out_buffer->m_buffer = plugin_data->m_recv_buffer;
                out_buffer->m_length = in_hdr->payload_len;
            }
            else if (in_hdr->msg_type == MSG_TYPE_DATA_BLOCK_CHUNK)
            {
                ASSERT(in_hdr->payload_len >= sizeof(download_block_header_t));
                out_buffer->m_buffer = plugin_data->m_recv_buffer;
                out_buffer->m_length = in_hdr->payload_len;  // Should be sizeof(payload_download_t)
            }

            return true;  // Handled download message
        }

        void download_commit_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* hdr, buffer_t buffer)
        {
            download_plugin_data_t* plugin_data = (download_plugin_data_t*)plugin->m_plugin_data;

            if (hdr->msg_type == MSG_TYPE_DATA_BLOCK_INIT)
            {
                download_init_t* init_payload     = (download_init_t*)buffer.m_buffer;
                plugin_data->m_data_type          = init_payload->data_type;
                plugin_data->m_target_buffer_size = init_payload->total_size;

                plugin_data->m_on_user_acquire(plugin->m_user_ctx, plugin_data->m_data_type, plugin_data->m_target_buffer_size, plugin_data->m_target_buffer);

                download_init_ack_t ack_msg;
                ack_msg.Magic       = 0xF00D;
                ack_msg.Type        = MSG_TYPE_DATA_BLOCK_INIT_ACK;
                ack_msg.PayloadSize = sizeof(download_init_ack_t) - sizeof(msg_hdr_t);
                ack_msg.Checksum    = 0;                                             // No checksum
                ack_msg.Status      = (plugin_data->m_target_buffer) ? 0x01 : 0x00;  // 0x01 = Ready to receive, 0x00 = Out of memory/Error

                const u8* mac = get_mac_address(plugin->m_wifi_mgr);
                g_memcpy(ack_msg.Mac, mac, 6);

                nnet::send_later(*plugin->m_client, (byte*)&ack_msg, sizeof(download_init_ack_t));
            }
            else if (hdr->msg_type == MSG_TYPE_DATA_BLOCK_CHUNK)
            {
                download_block_header_t* block_header = (download_block_header_t*)buffer.m_buffer;
                const byte*              data         = buffer.m_buffer + sizeof(download_block_header_t);

                // Copy the received block into the target buffer at the specified offset
                if (plugin_data->m_target_buffer && block_header->file_offset + block_header->block_size <= plugin_data->m_target_buffer_size)
                {
                    // TODO; Verify the block integrity here (e.g., checksum) before copying it to the target buffer.

                    g_memcpy(plugin_data->m_target_buffer + block_header->file_offset, data, block_header->block_size);

                    download_block_ack_t ack_msg;
                    ack_msg.Magic       = 0xF00D;
                    ack_msg.Type        = MSG_TYPE_DATA_BLOCK_CHUNK_ACK;
                    ack_msg.PayloadSize = sizeof(download_block_ack_t) - sizeof(msg_hdr_t);
                    ack_msg.Checksum    = 0;  // No checksum
                    ack_msg.BlockIndex  = block_header->block_index;
                    ack_msg.Status      = 0x01;  // Success

                    const u8* mac = get_mac_address(plugin->m_wifi_mgr);
                    g_memcpy(ack_msg.Mac, mac, 6);

                    nnet::send_later(*plugin->m_client, (byte*)&ack_msg, sizeof(download_block_ack_t));

                    plugin_data->m_received_blocks++;
                }

                // Was this the last block? If so, call the on_complete callback
                if (plugin_data->m_received_blocks == plugin_data->m_total_blocks)
                {
                    if (plugin->m_on_user_complete)
                    {
                        plugin->m_on_user_complete(plugin->m_user_ctx, plugin_data->m_data_type, plugin_data->m_target_buffer_size, plugin_data->m_target_buffer);

                        // We are done downloading, so we can free the receive buffer.
                        nsystem::free(plugin_data->m_recv_buffer);
                        plugin_data->m_recv_buffer = nullptr;

                        // The target buffer is now owned by the user and should be freed by them when done.
                        plugin_data->m_target_buffer      = nullptr;
                        plugin_data->m_target_buffer_size = 0;
                    }
                }
            }
        }

        void download_abort_fn(tcp_recv_plugin_t* plugin)
        {
            // Handle any cleanup or state reset if the handshake is aborted
            download_plugin_data_t* plugin_data = (download_plugin_data_t*)plugin->m_plugin_data;
            if (plugin_data->m_recv_buffer)
            {
                nsystem::free(plugin_data->m_recv_buffer);
                plugin_data->m_recv_buffer = nullptr;
            }
            if (plugin_data->m_target_buffer)
            {
                nsystem::free(plugin_data->m_target_buffer);
                plugin_data->m_target_buffer      = nullptr;
                plugin_data->m_target_buffer_size = 0;
            }
        }

        // There can only be one download plugin active at a time

        tcp_recv_plugin_t* new_download_plugin(tcp_recv_begin_fn on_begin, tcp_recv_complete_fn on_complete, void* user_ctx)
        {
            download_plugin_data_t* data = (download_plugin_data_t*)nsystem::calloc(sizeof(download_plugin_data_t));
            data->m_user_ctx             = user_ctx;
            data->m_on_user_acquire      = on_begin;
            data->m_on_user_complete     = on_complete;
            data->m_recv_buffer          = nullptr;  // Buffer for receiving blocks of data
            data->m_target_buffer        = nullptr;  // Destination will be set when receiving the first block
            data->m_target_buffer_size   = 0;
            data->m_total_blocks         = 0;
            data->m_received_blocks      = 0;

            tcp_recv_plugin_t* plugin = (tcp_recv_plugin_t*)nsystem::calloc(sizeof(tcp_recv_plugin_t));
            plugin->m_plugin_data     = data;
            plugin->m_acquire         = download_acquire_fn;
            plugin->m_commit          = download_commit_fn;
            plugin->m_abort           = download_abort_fn;
            return plugin;
        }

        void destroy_download_plugin(tcp_recv_plugin_t* plugin)
        {
            download_plugin_data_t* plugin_data = (download_plugin_data_t*)plugin->m_plugin_data;
            if (plugin_data->m_recv_buffer)
            {
                nsystem::free(plugin_data->m_recv_buffer);
                plugin_data->m_recv_buffer = nullptr;
            }

            nsystem::free(plugin_data);
            nsystem::free(plugin);
        }

        // 888b     d888 8888888888 .d8888b.   .d8888b.        d8888  .d8888b.  8888888888 .d8888b.
        // 8888b   d8888 888       d88P  Y88b d88P  Y88b      d88888 d88P  Y88b 888       d88P  Y88b
        // 88888b.d88888 888       Y88b.      Y88b.          d88P888 888    888 888       Y88b.
        // 888Y88888P888 8888888    "Y888b.    "Y888b.      d88P 888 888        8888888    "Y888b.
        // 888 Y888P 888 888           "Y88b.     "Y88b.   d88P  888 888  88888 888           "Y88b.
        // 888  Y8P  888 888             "888       "888  d88P   888 888    888 888             "888
        // 888   "   888 888       Y88b  d88P Y88b  d88P d8888888888 Y88b  d88P 888       Y88b  d88P
        // 888       888 8888888888 "Y8888P"   "Y8888P" d88P     888  "Y8888P88 8888888888 "Y8888P"

        struct message_plugin_data_t
        {
            void*                     m_user_ctx;
            tcp_recv_user_acquire_fn  m_on_user_acquire;
            tcp_recv_user_complete_fn m_on_user_complete;
            byte*                     m_target_buffer;       // Final destination for a message
            u32                       m_target_buffer_size;  // Size of the PSRAM buffer
            u32                       m_data_type;           // Remember data type
        };

        bool message_acquire_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* in_hdr, buffer_t* out_buffer)
        {
            message_plugin_data_t* plugin_data = (message_plugin_data_t*)plugin->m_plugin_data;
            plugin_data->m_data_type           = in_hdr->m_data_type;
            plugin_data->m_target_buffer       = nullptr;
            plugin_data->m_target_buffer_size  = in_hdr->PayloadSize;

            plugin_data->m_on_user_acquire(plugin->m_user_ctx, plugin_data->m_data_type, plugin_data->m_target_buffer_size, plugin_data->m_target_buffer);

            out_buffer->m_data      = plugin_data->m_target_buffer;
            out_buffer->m_data_size = plugin_data->m_target_buffer_size;

            return true;  // Handled download message
        }

        void message_commit_fn(tcp_recv_plugin_t* plugin, msg_hdr_t* hdr, buffer_t buffer)
        {
            message_plugin_data_t* plugin_data = (message_plugin_data_t*)plugin->m_plugin_data;
            plugin_data->m_on_user_complete(plugin->m_user_ctx, plugin_data->m_data_type, buffer.m_data_size, buffer.m_data);
            plugin_data->m_target_buffer      = nullptr;
            plugin_data->m_target_buffer_size = 0;
        }

        void message_abort_fn(tcp_recv_plugin_t* plugin)
        {
            message_plugin_data_t* plugin_data = (message_plugin_data_t*)plugin->m_plugin_data;
            plugin_data->m_target_buffer       = nullptr;
            plugin_data->m_target_buffer_size  = 0;
        }

        tcp_recv_plugin_t* new_messages_plugin(tcp_recv_begin_fn on_begin, tcp_recv_complete_fn on_complete, void* user_ctx)
        {
            message_plugin_data_t* data = (message_plugin_data_t*)nsystem::malloc(sizeof(message_plugin_data_t));
            data->m_user_ctx            = user_ctx;
            data->m_on_user_acquire     = on_begin;
            data->m_on_user_complete    = on_complete;
            tcp_recv_plugin_t* plugin   = (tcp_recv_plugin_t*)nsystem::malloc(sizeof(tcp_recv_plugin_t));
            plugin->m_plugin_data       = data;
            plugin->m_acquire           = message_acquire_fn;
            plugin->m_commit            = message_commit_fn;
            plugin->m_abort             = message_abort_fn;
            return plugin;
        }

        void destroy_messages_plugin(tcp_recv_plugin_t* plugin)
        {
            if (plugin)
            {
                if (plugin->m_plugin_data)
                {
                    nsystem::free(plugin->m_plugin_data);
                }
                nsystem::free(plugin);
            }
        }

    }  // namespace nnet
}  // namespace ncore
