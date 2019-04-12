#include <spin1_api.h>
#include <debug.h>
#include <bit_field.h>
#include <sdp_no_scp.h>
#include "common/platform.h"
#include "common/routing_table.h"
#include "common/sdp_formats.h"
#include "common/constants.h"

#include "common-typedefs.h"
#include "compressor_includes/aliases.h"
#include "compressor_includes/ordered_covering.h"
/*****************************************************************************/
/* SpiNNaker routing table minimisation with bitfield integration.
 *
 * Minimise a routing table loaded into SDRAM and load the minimised table into
 * the router using the specified application ID.
 *
 * the exit code is stored in the user1 register
 *
 * The memory address with tag "1" is expected contain the following struct
 * (entry_t is defined in `routing_table.h` but is described below).
*/

//! interrupt priorities
typedef enum interrupt_priority{
    TIMER_TICK_PRIORITY = -1,
    SDP_PRIORITY = 0,
    COMPRESSION_START_PRIORITY = 2
} interrupt_priority;

//! \timer controls, as it seems timer in massive waits doesnt engage properly
int counter = 0;
int max_counter = 0;

//! \brief the timer control logic.
volatile bool timer_for_compression_attempt = false;

//! \brief number of times a compression time slot has occurred
bool *finish_compression_flag = false;

//! \brief flag saying if we've sent a force ack, incase we get many of them.
bool sent_force_ack = false;

//! \brief bool flag to say if i was forced to stop by the compressor control
bool *finished_by_compressor_force = false;

//! bool flag pointer to allow minimise to report if it failed due to malloc
//! issues
bool *failed_by_malloc = false;

//! control flag for running compression only when needed
bool compress_only_when_needed = false;

//! control flag for compressing as much as possible
bool compress_as_much_as_possible = false;

//! control flag if the routing tables are able to be stored in somewhere.
bool storable_routing_tables = false;

//! \brief the sdram location to write the compressed router table into
address_t sdram_loc_for_compressed_entries;

//! how many packets waiting for
uint32_t number_of_packets_waiting_for = 0;

//! \brief the control core id for sending responses to
uint32_t control_core_id = 1;

//! \brief sdp message to send acks to the control core with
sdp_msg_pure_data my_msg;

//! \brief sdp message data as a response packet (reducing casts)
response_sdp_packet_t* response = (response_sdp_packet_t*) &my_msg.data;

// ---------------------------------------------------------------------

//! \brief sends a sdp message back to the control core
void send_sdp_message_response(void) {
    my_msg.dest_port = (RANDOM_PORT << PORT_SHIFT) | control_core_id;
    // send sdp packet
    while (!spin1_send_sdp_msg((sdp_msg_t *) &my_msg, _SDP_TIMEOUT)) {
        log_info("failed to send. trying again");
        // Empty body
    }
}

//! \brief send a failed response due to a malloc issue
void return_malloc_response_message(void) {
    // set message ack finished state to malloc fail
    response->response_code = FAILED_MALLOC;

    // send message
    //send_sdp_message_response();
}

//! \brief send a success response message
void return_success_response_message(void) {
    // set message ack finished state to malloc fail
    response->response_code = SUCCESSFUL_COMPRESSION;

    // send message
    send_sdp_message_response();
    log_info("send success ack");
    //sark_io_buf_reset();
}

//! \brief send a failed response due to the control forcing it to stop
void return_failed_by_force_response_message(void) {
       // set message ack finished state to malloc fail
    response->response_code = FORCED_BY_COMPRESSOR_CONTROL;

    // send message
    send_sdp_message_response();
    //sark_io_buf_reset();
}

//! \brief sends a failed response due to running out of time
void return_failed_by_time_response_message(void) {
       // set message ack finished state to malloc fail
    response->response_code = RAN_OUT_OF_TIME;

    // send message
    send_sdp_message_response();
    //sark_io_buf_reset();
}

//! \brief send a failed response where finished compression but failed to
//! fit into allocated size.
void return_failed_by_space_response_message(void) {
       // set message ack finished state to malloc fail
    response->response_code = FAILED_TO_COMPRESS;

    // send message
    send_sdp_message_response();
    //sark_io_buf_reset();
}

//! \brief stores the compressed routing tables into the compressed sdram
//! location
//! \returns bool if was successful or now
bool store_into_compressed_address(void) {
    if (routing_table_sdram_get_n_entries() > TARGET_LENGTH) {
        log_error("not enough space in routing table");
        return false;
    }

    log_info("starting store of %d tables", n_tables);
    bool success = routing_table_sdram_store(
        sdram_loc_for_compressed_entries);
    log_info("finished store");
    if (!success) {
        log_error("failed to store entries into sdram.");
        return false;
    }
    return true;
}

//! \brief starts the compression process
void start_compression_process(uint unused0, uint unused1) {
    // api requirement
    use(unused0);
    use(unused1);

    // reset fail state flags
    spin1_pause();
    //log_info("in compression phase");
    *failed_by_malloc = false;
    *finished_by_compressor_force = false;
    timer_for_compression_attempt = false;

    // create aliases
    aliases_t aliases = aliases_init();

    // reset timer
    spin1_resume(SYNC_NOWAIT);

    // run compression
    bool success = oc_minimise(
        1023, &aliases, failed_by_malloc,
        finished_by_compressor_force, &timer_for_compression_attempt,
        finish_compression_flag, compress_only_when_needed,
        compress_as_much_as_possible);

    spin1_pause();
    //log_info("finished oc minimise with success %d", success);

    // check state
    //log_info("success was %d", success);
    if (success) {
        //log_info("store into compressed");
        success = store_into_compressed_address();
        if (success) {
            //log_info("success response");
            return_success_response_message();
        } else {
            //log_info("failed by space response");
            return_failed_by_space_response_message();
        }
        routing_table_reset();
    } else {  // if not a success, could be one of 4 states
        if (failed_by_malloc) {  // malloc failed somewhere
            //log_info("failed malloc response");
            return_malloc_response_message();
        } else if (finished_by_compressor_force) {  // control killed it
            //log_info("force fail response");
            if (!sent_force_ack) {
                return_failed_by_force_response_message();
                sent_force_ack = true;
                //log_info("send ack");
            } else {
                //log_info("ignoring as already sent ack");
            }
        } else if (timer_for_compression_attempt) {  // ran out of time
            //log_info("time fail response");
            return_failed_by_time_response_message();
        } else { // after finishing compression, still could not fit into table.
            //log_info("failed by space response");
            return_failed_by_space_response_message();
        }
    }
}

//! \brief takes a array of tables from a packet and puts them into the dtcm
//! store of routing tables based off a given offset
//! \param[in] n_tables_in_packet: the number of tables in packet to pull
//! \param[in] tables: the tables from the packet.
void store_info_table_store(int n_tables_in_packet, address_t tables[]) {
    for(int rt_index = 0; rt_index < n_tables_in_packet; rt_index++) {
        //log_info("address of table is %x",  tables[rt_index]);
        routing_tables_store_routing_table((table_t*) tables[rt_index]);
        //log_info("stored table with %d entries", tables[rt_index][0]);
    }
}

static void handle_start_data_stream(start_stream_sdp_packet_t *first_cmd) {
    // update response tracker
    sent_force_ack = false;
    routing_table_reset();

    // location where to store the compressed (size
    sdram_loc_for_compressed_entries = first_cmd->address_for_compressed;

    // set up fake heap
    //log_info("setting up fake heap for sdram usage");
    platform_new_heap_creation(first_cmd->fake_heap_data);
    //log_info("finished setting up fake heap for sdram usage");

    // set up packet tracker
    number_of_packets_waiting_for = first_cmd->n_sdp_packets_till_delivered;

    storable_routing_tables = routing_tables_init(
        first_cmd->total_n_tables);

    if (!storable_routing_tables) {
        log_error("failed to allocate memory for routing table.h state");
        return_malloc_response_message();
        return;
    }

    // store this set into the store
    //log_info("store routing table addresses into store");
    //log_info(
    //    "there are %d addresses in packet", first_cmd->n_tables_in_packet);
    //for (int i = 0; i < first_cmd->n_tables_in_packet; i++) {
    //    log_info("address is %x for %d", first_cmd->tables[i], i);
    //}
    store_info_table_store(first_cmd->n_tables_in_packet, first_cmd->tables);

    // keep tracker updated
    //log_info("finished storing routing table address into store");

    // if no more packets to locate, then start compression process
    if (--number_of_packets_waiting_for == 0) {
        routing_tables_print_out_table_sizes();
        spin1_schedule_callback(
            start_compression_process, 0, 0, COMPRESSION_START_PRIORITY);
    }
}

static void handle_extra_data_stream(extra_stream_sdp_packet_t *extra_cmd) {
    if (!storable_routing_tables) {
        log_error(
            "ignore extra routing table addresses packet, as cant store them");
        return;
    }

    // store this set into the store
    //log_info("store extra routing table addresses into store");
    store_info_table_store(extra_cmd->n_tables_in_packet, extra_cmd->tables);
    //log_info("finished storing extra routing table address into store");

    // if no more packets to locate, then start compression process
    if (--number_of_packets_waiting_for == 0) {
        spin1_schedule_callback(
            start_compression_process, 0, 0,
            COMPRESSION_START_PRIORITY);
    }
}

//! \brief the sdp control entrance.
//! \param[in] mailbox: the message
//! \param[in] port: don't care.
void _sdp_handler(uint mailbox, uint port) {
    use(port);

    //log_info("received packet");
    // get data from the sdp message
    sdp_msg_pure_data *msg = (sdp_msg_pure_data *) mailbox;
    compressor_payload_t *payload = (compressor_payload_t *) msg->data;
    // record control core.
    control_core_id = (msg->srce_port & CPU_MASK);

    //log_info("control core is %d", control_core_id);
    //log_info("command code is %d", payload->command);

    // get command code
    if (msg->srce_port >> PORT_SHIFT == RANDOM_PORT) {
        switch (payload->command) {
            case START_DATA_STREAM:
                handle_start_data_stream(&payload->start.msg);
                sark_msg_free((sdp_msg_t*) msg);
                break;
            case EXTRA_DATA_STREAM:
                handle_extra_data_stream(&payload->extra.msg);
                sark_msg_free((sdp_msg_t*) msg);
                break;
            case COMPRESSION_RESPONSE:
                //log_error("I really should not be receiving this!!! WTF");
                sark_msg_free((sdp_msg_t*) msg);
                break;
            case STOP_COMPRESSION_ATTEMPT:
                //log_info("been forced to stop by control");
                *finished_by_compressor_force = true;
                sark_msg_free((sdp_msg_t*) msg);
                break;
            default:
                log_error(
                    "no idea what to do with message with command code %d; "
                    "Ignoring", payload->command);
                sark_msg_free((sdp_msg_t*) msg);
        }
    } else {
        log_error(
            "no idea what to do with message. on port %d; Ignoring",
            msg->srce_port >> PORT_SHIFT);
        sark_msg_free((sdp_msg_t*) msg);
    }
}

//! \brief timer interrupt for controlling time taken to try to compress table
//! \param[in] unused0: not used
//! \param[in] unused1: not used
void timer_callback(uint unused0, uint unused1) {
    use(unused0);
    use(unused1);
    counter ++;

    if (counter >= max_counter){
        *finish_compression_flag = true;
        //log_info("passed timer point");
        spin1_pause();
    }
}

//! \brief the callback for setting off the router compressor
void initialise(void) {
    //log_info("Setting up stuff to allow bitfield compressor to occur.");

    //log_info("reading time_for_compression_attempt");
    vcpu_t *sark_virtual_processor_info = (vcpu_t*) SV_VCPU;
    vcpu_t *this_processor = &sark_virtual_processor_info[spin1_get_core_id()];

    uint32_t time_for_compression_attempt = this_processor->user1;
    //log_info("user 1 = %d", time_for_compression_attempt);

    // bool from int conversion happening here
    uint32_t int_value = this_processor->user2;
    //log_info("user 2 = %d", int_value);
    if (int_value == 1) {
        compress_only_when_needed = true;
    }

    int_value = this_processor->user3;
    //log_info("user 3 = %d", int_value);
    if (int_value == 1) {
        compress_as_much_as_possible = true;
    }

    max_counter = time_for_compression_attempt / 1000;

    spin1_set_timer_tick(1000);
    spin1_callback_on(TIMER_TICK, timer_callback, TIMER_TICK_PRIORITY);

    //log_info("set up sdp interrupt");
    spin1_callback_on(SDP_PACKET_RX, _sdp_handler, SDP_PRIORITY);
    //log_info("finished sdp interrupt");

    //log_info("set up sdp message bits");
    response->command_code = COMPRESSION_RESPONSE;
    my_msg.flags = REPLY_NOT_EXPECTED;
    my_msg.srce_addr = spin1_get_chip_id();
    my_msg.dest_addr = spin1_get_chip_id();
    my_msg.srce_port = (RANDOM_PORT << PORT_SHIFT) | spin1_get_core_id();
    my_msg.length = LENGTH_OF_SDP_HEADER + (sizeof(response_sdp_packet_t));
    //log_info("finished sdp message bits");
    //log_info("my core id is %d", spin1_get_core_id());
    //log_info(
    //    "srce_port = %d the core id is %d",
    //    my_msg.srce_port, my_msg.srce_port & CPU_MASK);
}

//! \brief the main entrance.
void c_main(void) {
    //log_info("%u bytes of free DTCM", sark_heap_max(sark.heap, 0));

    initialise();

    // go
    spin1_start(SYNC_WAIT);
}
