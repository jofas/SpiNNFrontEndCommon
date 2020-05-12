/*
 * Copyright (c) 2019-2020 The University of Manchester
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

#include <spin1_api.h>
#include <debug.h>
#include <bit_field.h>
#include <sdp_no_scp.h>
#include <malloc_extras.h>
#include "common/routing_table.h"
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
    COMPRESSION_START_PRIORITY = 2
} interrupt_priority;

//! \timer controls, as it seems timer in massive waits doesnt engage properly
int counter = 0;
int max_counter = 0;

//! \brief bool saying if the timer has fired, resulting in attempt to compress
//! shutting down
volatile bool timer_for_compression_attempt = false;

//! \brief bool flag to say if i was forced to stop by the compressor control
volatile bool finished_by_compressor_force = false;

//! bool flag pointer to allow minimise to report if it failed due to malloc
//! issues
bool failed_by_malloc = false;

//! control flag for running compression only when needed
bool compress_only_when_needed = false;

//! control flag for compressing as much as possible
bool compress_as_much_as_possible = false;

//! \brief aliases thingy for compression
aliases_t aliases;

//! n bitfields testing
int n_bit_fields = -1;

// values for debug logging in wait_for_instructions
int previous_sorter_state = 0;
int previous_compressor_state = 0;

comms_sdram_t *comms_sdram;
// ---------------------------------------------------------------------

//! \brief stores the compressed routing tables into the compressed sdram
//! location
//! \returns bool if was successful or now
bool store_into_compressed_address(void) {
    if (routing_table_sdram_get_n_entries() > TARGET_LENGTH) {
        log_debug("not enough space in routing table");
        return false;
    }

    log_debug(
        "starting store of %d tables with %d entries",
        n_tables, routing_table_sdram_get_n_entries());

    malloc_extras_check_all_marked(50003);

    bool success = routing_table_sdram_store(comms_sdram->compressed_table);
    malloc_extras_check_all_marked(50004);

    log_debug("finished store");
    if (!success) {
        log_error("failed to store entries into sdram.");
        return false;
    }
    return true;
}

//! \brief handles the compression process
//! \param[in] unused0: param 1 forced on us from api
//! \param[in] unused1: param 2 forced on us from api
void start_compression_process() {
    log_debug("in compression phase");

    // restart timer (also puts us in running state)
    spin1_resume(SYNC_NOWAIT);

    malloc_extras_check_all_marked(50001);

    // run compression
    bool success = oc_minimise(
        TARGET_LENGTH, &aliases, &failed_by_malloc,
        &comms_sdram->sorter_instruction,
        &timer_for_compression_attempt, compress_only_when_needed,
        compress_as_much_as_possible);

    // print out result for debugging purposes
    if (success) {
        log_info("Passed oc minimise with success %d", success);
    } else {
        log_info("Failed oc minimise with success %d", success);
    }
    malloc_extras_check_all_marked(50005);

    // turn off timer and set us into pause state
    spin1_pause();

    // check state
    log_debug("success was %d", success);
    if (success) {
        log_debug("store into compressed");
        success = store_into_compressed_address();
        if (success) {
            log_debug("success response");
            comms_sdram->compressor_state = SUCCESSFUL_COMPRESSION;
        } else {
            log_debug("failed by space response");
            comms_sdram->compressor_state = FAILED_TO_COMPRESS;
        }
    } else {  // if not a success, could be one of 4 states
        if (failed_by_malloc) {  // malloc failed somewhere
            log_debug("failed malloc response");
            comms_sdram->compressor_state = FAILED_MALLOC;
        } else if (comms_sdram->sorter_instruction != RUN) {  // control killed it
            log_debug("force fail response");
            comms_sdram->compressor_state = FORCED_BY_COMPRESSOR_CONTROL;
            log_debug("send ack");
        } else if (timer_for_compression_attempt) {  // ran out of time
            log_debug("time fail response");
            comms_sdram->compressor_state = RAN_OUT_OF_TIME;
        } else { // after finishing compression, still could not fit into table.
            log_debug("failed by space response");
            comms_sdram->compressor_state = FAILED_TO_COMPRESS;
        }
    }
}

void run_compression_process(void){

    log_debug("setting up fake heap for sdram usage");
    malloc_extras_initialise_with_fake_heap(comms_sdram->fake_heap_data);
    log_debug("set up fake heap for sdram usage");

    failed_by_malloc = false;
    timer_for_compression_attempt = false;
 // reset timer counter
    counter = 0;
    aliases_clear(&aliases);
    routing_table_reset();

    // create aliases
    aliases = aliases_init();

    malloc_extras_check_all_marked(50002);

    log_info("table init for %d tables", comms_sdram->n_elements);
    bool success = routing_tables_init(
        comms_sdram->n_elements, comms_sdram->elements);
    log_debug("table init finish");
    if (!success) {
        log_error("failed to allocate memory for routing table.h state");
        comms_sdram->compressor_state = FAILED_MALLOC;
        return;
    }

    log_info("starting compression attempt");
    log_debug("my processor id at start comp is %d", spin1_get_core_id());
    // start compression process
    start_compression_process();
}

static inline bool process_prepare(compressor_states compressor_state) {
    switch(compressor_state) {
        case UNUSED:
            // First prepare
            log_info("Prepared for the first time");
            comms_sdram->compressor_state = PREPARED;
            return true;
        case FAILED_MALLOC:
        case FORCED_BY_COMPRESSOR_CONTROL:
        case SUCCESSFUL_COMPRESSION:
        case FAILED_TO_COMPRESS:
        case RAN_OUT_OF_TIME:
            // clear previous result
            log_info("prepared");
            comms_sdram->compressor_state = PREPARED;
            return true;
        case PREPARED:
            // waiting for sorter to pick up result
            return true;
        case COMPRESSING:
            // Should never happen
            return false;
    }
    return false;
}

static inline bool process_run(compressor_states compressor_state) {

    switch(compressor_state) {
        case PREPARED:
            log_info("run detected");
            comms_sdram->compressor_state = COMPRESSING;
            run_compression_process();
            return true;
        case COMPRESSING:
            // Should not be back in this loop before result set
            return false;
        case FAILED_MALLOC:
        case FORCED_BY_COMPRESSOR_CONTROL:
        case SUCCESSFUL_COMPRESSION:
        case FAILED_TO_COMPRESS:
        case RAN_OUT_OF_TIME:
            // waiting for sorter to pick up result
            return true;
        case UNUSED:
            // Should never happen
            return false;
    }
    return false;
}

static inline bool process_force(compressor_states compressor_state) {
   switch(compressor_state) {
        case COMPRESSING:
            // passed to compressor as *sorter_instruction
            // Do nothing until compressor notices changed
            return true;
        case FAILED_MALLOC:
            // Keep force malloc as more important message
            return true;
        case FORCED_BY_COMPRESSOR_CONTROL:
            // Waiting for sorter to pick up
            return true;
        case SUCCESSFUL_COMPRESSION:
        case FAILED_TO_COMPRESS:
        case RAN_OUT_OF_TIME:
            log_info("Force detected");
            // The results other than MALLOC no longer matters
            comms_sdram->compressor_state = FORCED_BY_COMPRESSOR_CONTROL;
            return true;
        case PREPARED:
        case UNUSED:
            // Should never happen
            return false;
   }
   return false;
}

static inline bool process_none(compressor_states compressor_state) {
    switch(compressor_state) {
        case UNUSED:
            // waiting for sorter to malloc user1 and send prepare
            return true;
        case PREPARED:
        case COMPRESSING:
        case FAILED_MALLOC:
        case FORCED_BY_COMPRESSOR_CONTROL:
        case RAN_OUT_OF_TIME:
        case SUCCESSFUL_COMPRESSION:
        case FAILED_TO_COMPRESS:
            // Should never happen
            return false;
    }
    return false;
}

void wait_for_instructionsX(uint unused0, uint unused1) {
    //api requirements
    use(unused0);
    use(unused1);
    log_info("compressor_stat: %d, .sorter_instruction %d, n_elements %d "
            "n_bit_fields %d",
            comms_sdram->compressor_state,
            comms_sdram->sorter_instruction,
            comms_sdram->n_elements,
            comms_sdram->n_bit_fields);
}

//! \brief busy waits until there is a new instuction from the sorter
void wait_for_instructions(uint unused0, uint unused1) {
    //api requirements
    use(unused0);
    use(unused1);

    bool users_match = true;
    // set if combination of user2 and user3 is unexpected

    // cache the states so they dont change inside one loop
    compressor_states compressor_state = comms_sdram->compressor_state;
    instrucions_to_compressor sorter_state = comms_sdram->sorter_instruction;

    // Log if changed
    if (sorter_state != previous_sorter_state) {
         previous_sorter_state = sorter_state;
         log_info("Sorter state changed  sorter: %d compressor %d",
            sorter_state, compressor_state);
    }
    if (compressor_state != previous_compressor_state) {
        previous_compressor_state = compressor_state;
        log_info("Compressor state changed  sorter: %d compressor %d",
           sorter_state, compressor_state);
    }

    switch(sorter_state) {
        case PREPARE:
            users_match = process_prepare(compressor_state);
            break;
        case RUN:
            users_match = process_run(compressor_state);
            break;
        case FORCE_TO_STOP:
            users_match = process_force(compressor_state);
            break;
        case NONE:
            users_match = process_none(compressor_state);
            break;
    }
    if (users_match) {
        spin1_schedule_callback(
            wait_for_instructions, 0, 0, COMPRESSION_START_PRIORITY);
    } else {
        log_error("Unexpected combination of sorter_state %d and "
            "compressor_state %d",
                sorter_state, compressor_state);
            malloc_extras_terminate(RTE_SWERR);
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
        timer_for_compression_attempt = true;
        log_debug("passed timer point");
        spin1_pause();
    }
}

//! \brief the callback for setting off the router compressor
void initialise(void) {
    log_info("Setting up stuff to allow bitfield compressor to occur.");

    log_info("reading time_for_compression_attempt");
    vcpu_t *sark_virtual_processor_info = (vcpu_t *) SV_VCPU;
    vcpu_t *this_vcpu_info = &sark_virtual_processor_info[spin1_get_core_id()];

    uint32_t time_for_compression_attempt = this_vcpu_info->user1;
    log_info("user 1 = %d", time_for_compression_attempt);

    // bool from int conversion happening here
    uint32_t int_value = this_vcpu_info->user2;
    log_info("user 2 = %d", int_value);
    if (int_value == 1) {
        compress_only_when_needed = true;
    }
    // TODO compress_as_much_as_possible
    //int_value = this_processor->user3;
    //log_info("user 3 = %d", int_value);
    //if (int_value == 1) {
        compress_as_much_as_possible = false;
    //}

    // Get the pointer for all cores
    comms_sdram = (comms_sdram_t*)this_vcpu_info->user3;
    // Now move the pointer to the comms for this core
    comms_sdram += spin1_get_core_id();

    // sort out timer (this is done in a indirect way due to lack of trust to
    // have timer only fire after full time after pause and resume.
    max_counter = time_for_compression_attempt / 1000;
    spin1_set_timer_tick(1000);
    spin1_callback_on(TIMER_TICK, timer_callback, TIMER_TICK_PRIORITY);

    log_info("my processor id is %d", spin1_get_core_id());
}

//! \brief the main entrance.
void c_main(void) {
    log_info("%u bytes of free DTCM", sark_heap_max(sark.heap, 0));

    // set up params
    initialise();

    // kick-start the process
    spin1_schedule_callback(
        wait_for_instructions, 0, 0, COMPRESSION_START_PRIORITY);

    // go
    spin1_start(SYNC_WAIT);
}
