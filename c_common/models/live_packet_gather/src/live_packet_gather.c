/*
 * Copyright (c) 2017-2019 The University of Manchester
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

//! \file
//!
//! \brief The implementation of the Live Packet Gatherer.
//!
//! The purpose of this application is to allow recorded information to be
//! streamed out of SpiNNaker in real time. It does not scale very well, as
//! SpiNNaker's aggregate internal state can change with a much higher
//! bandwidth than its external networking can handle.

#include <common-typedefs.h>
#include <circular_buffer.h>
#include <data_specification.h>
#include <debug.h>
#include <simulation.h>
#include <spin1_api.h>
#include <eieio.h>

//! Provenance data store
struct provenance_data_struct {
    //! Count of overflows when no payload was sent
    uint32_t number_of_overflows_no_payload;
    //! Count of overflows when a payload was sent
    uint32_t number_of_overflows_with_payload;
};

//! \brief Definitions of each element in the configuration.
//!
//! This is copied from SDRAM into DTCM for speed.
struct lpg_config {
    //! P bit
    uint32_t apply_prefix;
    //! Prefix data
    uint32_t prefix;
    //! Type bits
    uint32_t prefix_type;
    //! F bit (for the receiver)
    uint32_t packet_type;
    //! Right payload shift (for the sender)
    uint32_t key_right_shift;
    //! T bit
    uint32_t payload_timestamp;
    //! D bit
    uint32_t payload_apply_prefix;
    //! Payload prefix data (for the receiver)
    uint32_t payload_prefix;
    //! Right payload shift (for the sender)
    uint32_t payload_right_shift;
    //! SDP tag to use when sending
    uint32_t sdp_tag;
    //! SDP destination to use when sending
    uint32_t sdp_dest;
    //! Maximum number of packets expected per timestep
    uint32_t packets_per_timestamp;
};

//! values for the priority for each callback
enum {
    MC_PACKET = -1, //!< Multicast packet interrupt uses FIQ (super high prio)
    SDP = 0,        //!< SDP interrupt is highest priority
    USER = 1,       //!< Interrupt for enqueued list of received packets
    DMA = 2,        //!< DMA complete interrupt is low priority
    TIMER = 3       //!< Timer interrupt is lowest priority
};

//! human readable definitions of each region in SDRAM
enum {
    SYSTEM_REGION,
    CONFIGURATION_REGION,
    PROVENANCE_REGION
};

//! EIEIO packet types
enum packet_types {
    NO_PAYLOAD_16,
    PAYLOAD_16,
    NO_PAYLOAD_32,
    PAYLOAD_32
};

// Globals
//! The SDP message that we will send.
static sdp_msg_t g_event_message;

//! The location of the EIEIO header in the message.
static uint16_t *sdp_msg_aer_header;

//! The location of the key prefix in the message. `NULL` if no prefix.
static uint16_t *sdp_msg_aer_key_prefix;

//! The location of the payload prefix in the message. `NULL` if no prefix.
static void *sdp_msg_aer_payload_prefix;

//! Pointer to outbound message data. _Might only be half-word aligned!_
static void *sdp_msg_aer_data;

//! Current simulation time
static uint32_t time;

//! Count of packets sent
static uint32_t packets_sent;

//! Index into our buffer in ::sdp_msg_aer_data
static uint32_t buffer_index;

//! Part of the generic EIEIO header that is constant
static uint16_t eieio_constant_header;

//! The size of an individual event
static uint8_t event_size;

//! The length of the header, in bytes
static uint8_t header_len;

//! When we will run until
static uint32_t simulation_ticks = 0;

//! \brief TRUE if we're running without bound.
//! FALSE if we're only running for a limited period of time.
static uint32_t infinite_run = FALSE;

//! Circular buffer of incoming multicast packets that lack payloads
static circular_buffer without_payload_buffer;

//! Circular buffer of incoming multicast packets that have payloads
static circular_buffer with_payload_buffer;

//! Whether we are processing events (or discarding them).
static bool processing_events = false;

//! The provenance information that we are collecting.
static struct provenance_data_struct provenance_data;

//! The configuration data of the application.
static struct lpg_config config;

//! How to test if a bit flag is set
#define FLAG_IS_SET(flags, bit)		(((flags) & (bit)) != 0)

//! How to use just the low 8 bits of an integer value
#define CLAMP8(value)				((value) & 0xFF)

//! How to use just the low 16 bits of an integer value
#define CLAMP16(value)				((value) & 0xFFFF)

//! Does the packet type include a payload?
#define HAVE_PAYLOAD(pkt_type)		FLAG_IS_SET(pkt_type, 0x1)

//! Does the packet type include a double-width payload?
#define HAVE_WIDE_LOAD(pkt_type)	FLAG_IS_SET(pkt_type, 0x2)

//! The size of the circular buffers.
#define BUFFER_CAPACITY 256

//! \brief Because _WHY OH WHY_ would you use aligned memory? At least with this
//! we don't get data aborts.
//! \param[out] base: Buffer to write in.
//!     _Only guaranteed to be half-word aligned._
//! \param[in] index: Offset in count of _words_ into the buffer.
//! \param[in] value: Value to write in (as little-endian).
static inline void write_unaligned(
        void *base, uint32_t index, uint32_t value) {
    uint16_t *ary = base;
    uint32_t idx = index * 2;
    ary[idx++] = CLAMP16(value);
    ary[idx] = CLAMP16(value >> 16);
}

//! \brief Send buffered events to host and clear internal buffers.
static void flush_events(void) {
    // Send the event message only if there is data
    if (buffer_index > 0) {
        uint8_t event_count;

        if ((config.packets_per_timestamp == 0)
                || (packets_sent < config.packets_per_timestamp)) {
            // Get the event count depending on if there is a payload or not
            if (HAVE_PAYLOAD(config.packet_type)) {
                event_count = buffer_index >> 1;
            } else {
                event_count = buffer_index;
            }

            // insert appropriate header
            sdp_msg_aer_header[0] = eieio_constant_header | CLAMP8(event_count);

            g_event_message.length =
                    sizeof(sdp_hdr_t) + header_len + event_count * event_size;

            if (config.payload_apply_prefix && config.payload_timestamp) {
                if (!HAVE_WIDE_LOAD(config.packet_type)) {
                    uint16_t *temp = sdp_msg_aer_payload_prefix;
                    temp[0] = CLAMP16(time);
                } else {
                    write_unaligned(sdp_msg_aer_payload_prefix, 0, time);
                }
            }

            spin1_send_sdp_msg(&g_event_message, 1);
            packets_sent++;
        }

        // reset packet content
        uint16_t words_to_clear = (buffer_index * event_size) >> 2;
        for (uint32_t i = 0; i < words_to_clear; i++) {
            write_unaligned(sdp_msg_aer_data, i, 0);
        }
    }

    // reset counter
    buffer_index = 0;
}

//! \brief Store provenance data elements into SDRAM
//! \param[out] provenance_region_address:
//!     Where the provenance data will be written
static void record_provenance_data(address_t provenance_region_address) {
    struct provenance_data_struct *sdram = (void *) provenance_region_address;
    // Copy provenance data into SDRAM region
    *sdram = provenance_data;
}

// Callbacks
//! \brief Periodic timer callback
//!
//! Forces all events to be sent at least on the timer tick (calling
//! flush_events()) and handles pausing as required.
//!
//! \param unused0: unused
//! \param unused1: unused
static void timer_callback(uint unused0, uint unused1) {
    use(unused0);
    use(unused1);

    // flush the spike message and sent it over the Ethernet
    flush_events();

    // increase time variable to keep track of current timestep
    time++;
    log_debug("Timer tick %u", time);

    // check if the simulation has run to completion
    if ((infinite_run != TRUE) && (time >= simulation_ticks)) {
        simulation_handle_pause_resume(NULL);

        // Subtract 1 from the time so this tick gets done again on the next
        // run
        time--;

        simulation_ready_to_read();
    }
}

//! \brief Flush events to the outside world if our internal buffers are now
//! full.
//!
//! Calls flush_events() to do the flush.
static void flush_events_if_full(void) {
    uint8_t event_count;

    if (HAVE_PAYLOAD(config.packet_type)) {
        event_count = buffer_index >> 1;
    } else {
        event_count = buffer_index;
    }

    if ((event_count + 1) * event_size > BUFFER_CAPACITY) {
        flush_events();
    }
}

//! \brief Processes an incoming multicast packet without payload.
//! \param[in] key: The key of the packet.
static void process_incoming_event(uint key) {
    log_debug("Processing key %x", key);

    // process the received spike
    if (!HAVE_WIDE_LOAD(config.packet_type)) {
        // 16 bit packet
        uint16_t *buf_pointer = sdp_msg_aer_data;
        buf_pointer[buffer_index++] = CLAMP16(key >> config.key_right_shift);

        // if there is a payload to be added
        if (HAVE_PAYLOAD(config.packet_type) && !config.payload_timestamp) {
            buf_pointer[buffer_index++] = 0;
        } else if (HAVE_PAYLOAD(config.packet_type) && config.payload_timestamp) {
            buf_pointer[buffer_index++] = CLAMP16(time);
        }
    } else {
        // 32 bit packet
        write_unaligned(sdp_msg_aer_data, buffer_index++, key);

        // if there is a payload to be added
        if (HAVE_PAYLOAD(config.packet_type) && !config.payload_timestamp) {
            write_unaligned(sdp_msg_aer_data, buffer_index++, 0);
        } else if (HAVE_PAYLOAD(config.packet_type) && config.payload_timestamp) {
            write_unaligned(sdp_msg_aer_data, buffer_index++, time);
        }
    }

    // send packet if enough data is stored
    flush_events_if_full();
}

//! \brief Processes an incoming multicast packet with payload.
//! \param[in] key: The key of the packet.
//! \param[in] payload: The payload word of the packet.
static void process_incoming_event_payload(uint key, uint payload) {
    log_debug("Processing key %x, payload %x", key, payload);

    // process the received spike
    if (!HAVE_WIDE_LOAD(config.packet_type)) {
        //16 bit packet
        uint16_t *buf_pointer = sdp_msg_aer_data;
        buf_pointer[buffer_index++] = CLAMP16(key >> config.key_right_shift);

        //if there is a payload to be added
        if (HAVE_PAYLOAD(config.packet_type) && !config.payload_timestamp) {
            buf_pointer[buffer_index++] = CLAMP16(payload >> config.payload_right_shift);
        } else if (HAVE_PAYLOAD(config.packet_type) && config.payload_timestamp) {
            buf_pointer[buffer_index++] = CLAMP16(time);
        }
    } else {
        //32 bit packet
        write_unaligned(sdp_msg_aer_data, buffer_index++, key);

        //if there is a payload to be added
        if (HAVE_PAYLOAD(config.packet_type) && !config.payload_timestamp) {
            write_unaligned(sdp_msg_aer_data, buffer_index++, payload);
        } else if (HAVE_PAYLOAD(config.packet_type) && config.payload_timestamp) {
            write_unaligned(sdp_msg_aer_data, buffer_index++, time);
        }
    }

    // send packet if enough data is stored
    flush_events_if_full();
}

//! \brief Handler for processing incoming packets that have been locally queued
//!
//! Triggered by calling spin1_trigger_user_event() in incoming_event_callback()
//! and incoming_event_payload_callback(), which (being attached to the FIQ)
//! just enqueue messages for later handling. Delegates to
//! process_incoming_event() and process_incoming_event_payload() for actual
//! processing.
//!
//! Packets without payload are slightly higher priority than packets with
//! payload.
//!
//! \param unused0: Ignored
//! \param unused1: Ignored
static void incoming_event_process_callback(uint unused0, uint unused1) {
    use(unused0);
    use(unused1);

    do {
        uint32_t key, payload;

        if (circular_buffer_get_next(without_payload_buffer, &key)) {
            process_incoming_event(key);
        } else if (circular_buffer_get_next(with_payload_buffer, &key)
                && circular_buffer_get_next(with_payload_buffer, &payload)) {
            process_incoming_event_payload(key, payload);
        } else {
            processing_events = false;
        }
    } while (processing_events);
}

//! \brief Handler for incoming packets without payload
//!
//! Just enqueues them for later handling by incoming_event_process_callback(),
//! which will hand off to process_incoming_event().
//!
//! \param[in] key: The key of the incoming packet.
//! \param unused: unused
static void incoming_event_callback(uint key, uint unused) {
    use(unused);
    log_debug("Received key %x", key);

    if (circular_buffer_add(without_payload_buffer, key)) {
        if (!processing_events) {
            processing_events = true;
            spin1_trigger_user_event(0, 0);
        }
    } else {
        provenance_data.number_of_overflows_no_payload++;
    }
}

//! \brief Handler for incoming packets with payload
//!
//! Just enqueues them for later handling by incoming_event_process_callback(),
//! which will hand off to process_incoming_event_payload().
//!
//! \param[in] key: The key of the incoming packet.
//! \param[in] payload: The payload word of the incoming packet.
static void incoming_event_payload_callback(uint key, uint payload) {
    log_debug("Received key %x, payload %x", key, payload);

    if (circular_buffer_add(with_payload_buffer, key)) {
        circular_buffer_add(with_payload_buffer, payload);
        if (!processing_events) {
            processing_events = true;
            spin1_trigger_user_event(0, 0);
        }
    } else {
        provenance_data.number_of_overflows_with_payload++;
    }
}

//! \brief Copies the application configuration from DSG SDRAM to DTCM.
//!
//! Note that it's faster to copy by field than to use spin1_memcpy()!
//!
//! \param[in] sdram_config: Where to copy from
static void read_parameters(struct lpg_config *sdram_config) {
    // P bit
    config.apply_prefix = sdram_config->apply_prefix;
    // Prefix data
    config.prefix = sdram_config->prefix;
    // F bit (for the receiver)
    config.prefix_type = sdram_config->prefix_type;
    // Type bits
    config.packet_type = sdram_config->packet_type;
    // Right packet shift (for the sender)
    config.key_right_shift = sdram_config->key_right_shift;
    // T bit
    config.payload_timestamp = sdram_config->payload_timestamp;
    // D bit
    config.payload_apply_prefix = sdram_config->payload_apply_prefix;
    // Payload prefix data (for the receiver)
    config.payload_prefix = sdram_config->payload_prefix;
    // Right payload shift (for the sender)
    config.payload_right_shift = sdram_config->payload_right_shift;
    config.sdp_tag = sdram_config->sdp_tag;
    config.sdp_dest = sdram_config->sdp_dest;
    config.packets_per_timestamp = sdram_config->packets_per_timestamp;

    log_info("apply_prefix: %d", config.apply_prefix);
    log_info("prefix: %08x", config.prefix);
    log_info("prefix_type: %d", config.prefix_type);
    log_info("packet_type: %d", config.packet_type);
    log_info("key_right_shift: %d", config.key_right_shift);
    log_info("payload_timestamp: %d", config.payload_timestamp);
    log_info("payload_apply_prefix: %d", config.payload_apply_prefix);
    log_info("payload_prefix: %08x", config.payload_prefix);
    log_info("payload_right_shift: %d", config.payload_right_shift);
    log_info("sdp_tag: %d", config.sdp_tag);
    log_info("sdp_dest: 0x%04x", config.sdp_dest);
    log_info("packets_per_timestamp: %d", config.packets_per_timestamp);
}

//! \brief Initialise the application.
//! \param[out] timer_period: Value for programming the timer ticks.
//! \result True if initialisation succeeds.
static bool initialize(uint32_t *timer_period) {
    // Get the address this core's DTCM data starts at from SRAM
    data_specification_metadata_t *ds_regions =
            data_specification_get_data_address();

    // Read the header
    if (!data_specification_read_header(ds_regions)) {
        return false;
    }

    // Get the timing details and set up the simulation interface
    if (!simulation_initialise(
            data_specification_get_region(SYSTEM_REGION, ds_regions),
            APPLICATION_NAME_HASH, timer_period, &simulation_ticks,
            &infinite_run, &time, SDP, DMA)) {
        return false;
    }
    simulation_set_provenance_function(
            record_provenance_data,
            data_specification_get_region(PROVENANCE_REGION, ds_regions));

    // Fix simulation ticks to be one extra timer period to soak up last events
    if (infinite_run != TRUE) {
        simulation_ticks++;
    }

    // Read the parameters
    read_parameters(
            data_specification_get_region(CONFIGURATION_REGION, ds_regions));
    return true;
}

//! \brief Sets up the header of the AER EIEIO data message.
//! \return The address of the message content.
//!     _Might be only half-word aligned._
static void *init_aer_header(void) {
    header_len = 2;

    // pointers for AER packet header, prefix and data
    void *temp_ptr;
    if (config.apply_prefix) {
        // pointer to key prefix
        sdp_msg_aer_key_prefix = &sdp_msg_aer_header[1];
        temp_ptr = &sdp_msg_aer_header[2];
        sdp_msg_aer_key_prefix[0] = (uint16_t) config.prefix;
        header_len += 2;
    } else {
        sdp_msg_aer_key_prefix = NULL;
        temp_ptr = &sdp_msg_aer_header[1];
    }

    if (config.payload_apply_prefix) {
        sdp_msg_aer_payload_prefix = temp_ptr;
        uint16_t *a = sdp_msg_aer_payload_prefix;

        log_debug("temp_ptr: %08x", temp_ptr);
        log_debug("a: %08x", a);

        // pointer to payload prefix
        sdp_msg_aer_payload_prefix = temp_ptr;

        if (!HAVE_WIDE_LOAD(config.packet_type)) {
            //16 bit payload prefix
            temp_ptr = &a[1];
            header_len += 2;
            if (!config.payload_timestamp) {
                // add payload prefix as required - not a timestamp
                a[0] = config.payload_prefix;
            }
            log_debug("16 bit - temp_ptr: %08x", temp_ptr);
        } else {
            //32 bit payload prefix
            temp_ptr = &a[2];
            header_len += 4;
            if (!config.payload_timestamp) {
                // add payload prefix as required - not a timestamp
                a[0] = CLAMP16(config.payload_prefix);
                a[1] = CLAMP16(config.payload_prefix >> 16);
            }
            log_debug("32 bit - temp_ptr: %08x", temp_ptr);
        }
    } else {
        sdp_msg_aer_payload_prefix = NULL;
    }

    return temp_ptr;
}

//! \brief Configures the system for SDP message sending.
//! \return False if the system fails sanity checking. True on success.
static bool configure_sdp_msg(void) {
    log_info("configure_sdp_msg");

    switch (config.packet_type) {
    case NO_PAYLOAD_16:
        event_size = 2;
        break;
    case PAYLOAD_16:
        event_size = 4;
        break;
    case NO_PAYLOAD_32:
        event_size = 4;
        break;
    case PAYLOAD_32:
        event_size = 8;
        break;
    default:
        log_error("unknown packet type: %d", config.packet_type);
        return false;
    }

    // initialise SDP header
    g_event_message.tag = config.sdp_tag;
    // No reply required
    g_event_message.flags = 0x07;
    // Chip 0,0
    g_event_message.dest_addr = config.sdp_dest;
    // Dump through Ethernet
    g_event_message.dest_port = PORT_ETH;
    // Set up monitoring address and port
    g_event_message.srce_addr = spin1_get_chip_id();
    g_event_message.srce_port = (3 << PORT_SHIFT) | spin1_get_core_id();

    // check incompatible options
    if (config.payload_timestamp && config.payload_apply_prefix
            && HAVE_PAYLOAD(config.packet_type)) {
        log_error("Timestamp can either be included as payload prefix or as"
                "payload to each key, not both");
        return false;
    }
    if (config.payload_timestamp && !config.payload_apply_prefix
            && !HAVE_PAYLOAD(config.packet_type)) {
        log_error("Timestamp can either be included as payload prefix or as"
                "payload to each key, but current configuration does not"
                "specify either of these");
        return false;
    }

    // initialise AER header
    // pointer to data space of containing SDP message
    sdp_msg_aer_header = &g_event_message.cmd_rc;

    eieio_constant_header = 0;
    eieio_constant_header |= config.apply_prefix << APPLY_PREFIX;
    eieio_constant_header |= config.prefix_type << PREFIX_UPPER;
    eieio_constant_header |= config.payload_apply_prefix << APPLY_PAYLOAD_PREFIX;
    eieio_constant_header |= config.payload_timestamp << PAYLOAD_IS_TIMESTAMP;
    eieio_constant_header |= config.packet_type << PACKET_TYPE;

    // pointer to write data; beware of alignment hazards!
    sdp_msg_aer_data = init_aer_header();

    log_debug("sdp_msg_aer_header: %08x", sdp_msg_aer_header);
    log_debug("sdp_msg_aer_key_prefix: %08x", sdp_msg_aer_key_prefix);
    log_debug("sdp_msg_aer_payload_prefix: %08x", sdp_msg_aer_payload_prefix);
    log_debug("sdp_msg_aer_data: %08x", sdp_msg_aer_data);

    packets_sent = 0;
    buffer_index = 0;

    return true;
}

//! Entry point
void c_main(void) {
    // Configure system
    uint32_t timer_period = 0;
    if (!initialize(&timer_period)) {
        log_error("Error in initialisation - exiting!");
        rt_error(RTE_SWERR);
    }

    // Configure SDP message
    if (!configure_sdp_msg()) {
        rt_error(RTE_SWERR);
    }

    // Set up circular buffers for multicast message reception
    without_payload_buffer = circular_buffer_initialize(BUFFER_CAPACITY);
    with_payload_buffer = circular_buffer_initialize(BUFFER_CAPACITY * 2);

    // Set timer_callback
    spin1_set_timer_tick(timer_period);

    // Register callbacks
    spin1_callback_on(MC_PACKET_RECEIVED, incoming_event_callback, MC_PACKET);
    spin1_callback_on(
            MCPL_PACKET_RECEIVED, incoming_event_payload_callback, MC_PACKET);
    spin1_callback_on(
            USER_EVENT, incoming_event_process_callback, USER);
    spin1_callback_on(TIMER_TICK, timer_callback, TIMER);

    // Start the time at "-1" so that the first tick will be 0
    time = UINT32_MAX;
    simulation_run();
}
