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

/*!
 * \file
 * \brief implementation of simulation.h
 */

#include "simulation.h"

#include <stdbool.h>
#include <debug.h>
#include <spin1_api_params.h>
#include <spin1_api.h>

// Import things from spin1_api that are not explicitly exposed //

//! \brief Wait for interrupt.
//! \details Will return to just after this point after an
//!     interrupt has been raised.
extern void spin1_wfi(void);

//! \brief Indicate whether the SYNC signal has been received.
//! \return 0 (false) if not received and 1 (true) if received.
extern uint resume_wait(void);

//! the pointer to the simulation time used by application models
static uint32_t *pointer_to_simulation_time;

//! the pointer to the flag for if it is a infinite run
static uint32_t *pointer_to_infinite_run;

//! the pointer to the current simulation time
static uint32_t *pointer_to_current_time;

//! the function call to run when extracting provenance data from the chip
static prov_callback_t stored_provenance_function = NULL;

//! the function call to run when received a exit command.
static exit_callback_t stored_exit_function = NULL;

//! the function call to run just before resuming a simulation
static resume_callback_t stored_resume_function = NULL;

//! the function call to run at the start of simulation
static start_callback_t stored_start_function = NULL;

//! the region ID for storing provenance data from the chip
static struct simulation_provenance *prov = NULL;

//! the list of SDP callbacks for ports
static callback_t sdp_callback[NUM_SDP_PORTS];

//! the list of DMA callbacks for DMA complete callbacks
static callback_t dma_complete_callbacks[MAX_DMA_CALLBACK_TAG];

//! Whether the simulation uses the timer or not (default true)
static bool uses_timer = true;

//! \brief Store basic provenance data
//! \return the address after which new provenance data can be stored
static void *simulation_store_provenance_data(void) {
    //! gets access to the diagnostics object from SARK
    extern diagnostics_t diagnostics;

    // store the data into the provenance data region
    prov->transmission_event_overflow = diagnostics.tx_packet_queue_full;
    prov->callback_queue_overloads = diagnostics.task_queue_full;
    prov->dma_queue_overloads = diagnostics.dma_queue_full;
    prov->timer_tic_has_overrun =
            diagnostics.total_times_tick_tic_callback_overran;
    prov->max_num_timer_tic_overrun =
            diagnostics.largest_number_of_concurrent_timer_tic_overruns;
    return prov->provenance_data_elements;
}

//! \brief Run the provenance data storage
static void execute_provenance_storage(void) {
    if (prov != NULL) {
        log_info("Starting basic provenance gathering");
        void *address_to_start_with = simulation_store_provenance_data();
        if (stored_provenance_function != NULL) {
            log_info("running other provenance gathering");
            stored_provenance_function(address_to_start_with);
        }
    }
}

void simulation_run(void) {
    // go into spin1 API start, but paused (no SYNC yet)
    spin1_start_paused();
}

void simulation_handle_pause_resume(resume_callback_t callback) {
    // Pause the simulation
    if (uses_timer) {
        spin1_pause();
    }

    stored_resume_function = callback;

    // Store provenance data as required
    execute_provenance_storage();
}

//! \brief Exit the application.
//! \details Helper when not using the auto pause and resume functionality
void simulation_exit(void) {
    simulation_handle_pause_resume(NULL);
}

void simulation_ready_to_read(void) {
    sark_cpu_state(CPU_STATE_WAIT);
}

//! \brief Send an OK response to the host when a command message is received.
//! \param[in] msg: the message object to send to the host.
static void send_ok_response(sdp_msg_t *msg) {
    msg->cmd_rc = RC_OK;
    msg->length = 12;
    uint dest_port = msg->dest_port;
    uint dest_addr = msg->dest_addr;
    msg->dest_port = msg->srce_port;
    msg->srce_port = dest_port;
    msg->dest_addr = msg->srce_addr;
    msg->srce_addr = dest_addr;
    spin1_send_sdp_msg(msg, 10);
}

//! \brief Callback when starting after synchronise
//! \param unused0: unused
//! \param unused1: unused
static void synchronise_start(uint unused0, uint unused1) {
    while (resume_wait()) {
        spin1_wfi();
    }
    sark_cpu_state(CPU_STATE_RUN);
    stored_start_function();

    // If we are not using the timer, no-one else resets the event, so do it now
    // (event comes from sark.h - this is how SARK knows whether to wait for a
    //  SYNC0 or SYNC1 message)
    if (!uses_timer) {
        event.wait ^= 1;
    }
}

//! \brief Handle the new commands needed to resume the binary with a new
//!     runtime counter, as well as switching off the binary when it truly
//!     needs to be stopped.
//! \param[in] mailbox: The mailbox containing the SDP packet received
//! \param[in] port: The port on which the packet was received
static void simulation_control_scp_callback(uint mailbox, uint port) {
    use(port);
    sdp_msg_t *msg = (sdp_msg_t *) mailbox;
    uint16_t length = msg->length;

    switch (msg->cmd_rc) {
    case CMD_STOP:
        log_info("Received exit signal. Program complete.");

        // free the message to stop overload
        spin1_msg_free(msg);

        // call any stored exit callbacks
        if (stored_exit_function != NULL) {
            log_info("Calling pre-exit function");
            stored_exit_function();
        }
        log_info("Exiting");
        spin1_exit(0);
        break;

    case CMD_RUNTIME:
        log_info("Setting the runtime of this model to %d starting at %d",
                msg->arg1, msg->arg3);
        log_info("Setting the flag of infinite run for this model to %d",
                msg->arg2);

        // resetting the simulation time pointer
        *pointer_to_simulation_time = msg->arg1;
        *pointer_to_infinite_run = msg->arg2;
        // We start at time - 1 because the first thing models do is
        // increment a time counter
        *pointer_to_current_time = (msg->arg3 - 1);

        if (stored_resume_function != NULL) {
            log_info("Calling pre-resume function");
            stored_resume_function();
            stored_resume_function = NULL;
        }

        if (stored_start_function != NULL) {
            spin1_schedule_callback(synchronise_start, 0, 0, 1);
        }
        if (uses_timer) {
            log_info("Resuming");
            spin1_resume(SYNC_WAIT);
        } else {
            if (event.wait) {
                sark_cpu_state(CPU_STATE_SYNC1);
            } else {
                sark_cpu_state(CPU_STATE_SYNC0);
            }
        }


        // If we are told to send a response, send it now
        if (msg->data[0] == 1) {
            send_ok_response(msg);
        }

        // free the message to stop overload
        spin1_msg_free(msg);
        break;

    case PROVENANCE_DATA_GATHERING:
        log_info("Forced provenance gathering");

        // force provenance to be executed and then exit
        execute_provenance_storage();

        // call any stored exit callbacks
        if (stored_exit_function != NULL) {
            log_info("Calling pre-exit function");
            stored_exit_function();
        }
        spin1_msg_free(msg);
        spin1_exit(1);
        break;

    case IOBUF_CLEAR:
        // run clear iobuf code
        sark_io_buf_reset();

        // If we are told to send a response, send it now
        if (msg->arg3 == 1) {
            send_ok_response(msg);
        }

        // free the message to stop overload
        spin1_msg_free(msg);
        break;

    default:
        // should never get here
        log_error("received packet with unknown command code %d",
                msg->cmd_rc);
        spin1_msg_free(msg);
    }
}

//! \brief handles the SDP callbacks interface.
//! \param[in,out] mailbox: The pointer to the received message
//! \param[in] port: What port the message was received on
static void simulation_sdp_callback_handler(uint mailbox, uint port) {
    if (sdp_callback[port] != NULL) {
        // if a callback is associated with the port, process it
        sdp_callback[port](mailbox, port);
    } else {
        // if no callback is associated, dump the received packet
        sdp_msg_t *msg = (sdp_msg_t *) mailbox;
        sark_msg_free(msg);
    }
}

bool simulation_sdp_callback_on(uint sdp_port, callback_t callback) {
    if (sdp_callback[sdp_port] != NULL) {
        log_error("Cannot allocate SDP callback on port %d as its already "
                "been allocated.", sdp_port);
        return false;
    }
    sdp_callback[sdp_port] = callback;
    return true;
}

void simulation_sdp_callback_off(uint sdp_port) {
    sdp_callback[sdp_port] = NULL;
}

//! \brief handles the DMA transfer done callbacks interface.
//! \param[in] unused: unused
//! \param[in] tag: the tag of the DMA that completed
static void simulation_dma_transfer_done_callback(uint unused, uint tag) {
    if (tag < MAX_DMA_CALLBACK_TAG && dma_complete_callbacks[tag] != NULL) {
        dma_complete_callbacks[tag](unused, tag);
    }
}

bool simulation_dma_transfer_done_callback_on(uint tag, callback_t callback) {
    // ensure that tag being allocated is less than max tag
    if (tag >= MAX_DMA_CALLBACK_TAG) {
        log_error("Cannot handle tag value above %d, please reduce the tag "
                "value accordingly.", MAX_DMA_CALLBACK_TAG - 1);
        return false;
    }

    // allocate tag callback if not already allocated
    if (dma_complete_callbacks[tag] != NULL) {
        // if allocated already, raise error
        log_error("Cannot allocate DMA transfer callback on tag %d as its "
                "already been allocated.", tag);
        return false;
    }

    dma_complete_callbacks[tag] = callback;
    return true;
}

void simulation_dma_transfer_done_callback_off(uint tag) {
    dma_complete_callbacks[tag] = NULL;
}

bool simulation_initialise(
        address_t address, uint32_t expected_app_magic_number,
        uint32_t* timer_period, uint32_t *simulation_ticks_pointer,
        uint32_t *infinite_run_pointer, uint32_t *time_pointer,
        int sdp_packet_callback_priority,
        int dma_transfer_done_callback_priority) {
    struct simulation_config *config = (void *) address;

    // handle the timing reading
    if (config->application_magic_number != expected_app_magic_number) {
        log_error("Unexpected magic number 0x%08x instead of 0x%08x at 0x%08x",
                config->application_magic_number, expected_app_magic_number,
                &config->application_magic_number);
        return false;
    }

    if (sdp_packet_callback_priority < -1) {
        log_error("The SDP callback priority should be set to a number greater "
                "than or equal to -1.  It is currently set to %d",
                sdp_packet_callback_priority);
        return false;
    }

    // transfer data to pointers for end user usage
    *timer_period = config->timer_period;

    // handle the SDP callback for the simulation
    pointer_to_simulation_time = simulation_ticks_pointer;
    pointer_to_infinite_run = infinite_run_pointer;
    pointer_to_current_time = time_pointer;

    spin1_callback_on(SDP_PACKET_RX, simulation_sdp_callback_handler,
            sdp_packet_callback_priority);
    simulation_sdp_callback_on(config->control_sdp_port,
            simulation_control_scp_callback);
    spin1_callback_on(DMA_TRANSFER_DONE, simulation_dma_transfer_done_callback,
            dma_transfer_done_callback_priority);

    // if all simulation initialisation complete return true,
    return true;
}

void simulation_set_provenance_data_address(address_t provenance_data_address) {
    prov = (void *) provenance_data_address;
}

void simulation_set_provenance_function(
        prov_callback_t provenance_function,
        address_t provenance_data_address) {
    stored_provenance_function = provenance_function;
    prov = (void *) provenance_data_address;
}

void simulation_set_exit_function(exit_callback_t exit_function) {
    stored_exit_function = exit_function;
}

void simulation_set_start_function(start_callback_t start_function) {
    stored_start_function = start_function;
}

void simulation_set_uses_timer(bool sim_uses_timer) {
    uses_timer = sim_uses_timer;
}
