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

#ifndef __MALLOC_EXTRAS_H__
#define __MALLOC_EXTRAS_H__

#include <sark.h>
#include <common-typedefs.h>
#include <debug.h>

//============================================================================
//! ENUMS

//! enum for the different states to report through the user1 address.
typedef enum exit_states_for_user_one {
    EXITED_CLEANLY = 0, EXIT_FAIL = 1, EXIT_MALLOC = 2, EXIT_SWERR = 3,
    DETECTED_MALLOC_FAILURE = 4
} exit_states_for_user_one;

//============================================================================
//! INTERNAL STRUCTS

//! a SDRAM block outside the heap
typedef struct sdram_block {
    // the base address of where the SDRAM block starts
    uchar *sdram_base_address;

    // size of block in bytes
    uint size;
} sdram_block;

//! the struct for holding host based SDRAM blocks outside the heap
typedef struct available_sdram_blocks {
    // the number of blocks of SDRAM which can be utilised outside of alloc
    int n_blocks;

    // VLA of SDRAM blocks
    sdram_block blocks [];
} available_sdram_blocks;

//============================================================================
//! defines

//! \brief the bits were going to look for during safety checks.
#define SAFETY_FLAG 0xDEADBEEF

//! \brief the number of bytes (needs to be a multiple of 4) that we're going
//! to use to add trackers at the beginning of any malloc and flags put at
//! the end of every malloc, to try to detect memory overwrites.
#define EXTRA_BYTES 64

//! \brief standard constant of how many bytes are in a word
#define BYTE_TO_WORD 4

//! \brief the amount of memory hops at the end of the malloc is 4 less than
//! the total as the first 4 bytes of the malloc is the length the end user
//! actually asked for in words
#define MINUS_POINT EXTRA_BYTES - BYTE_TO_WORD

//! \brief the number of words to put at the end of any malloc, to try to
//! detect over writes.
#define BUFFER_WORDS MINUS_POINT / BYTE_TO_WORD

//! \brief the nim size of a malloc-able chunk of a heap which we will put
//! into the fake heap.
#define MIN_SIZE_HEAP 32

// ===========================================================================
//! functions

//! \brief turn on printing of logs. can reduce output significantly
void malloc_extras_turn_on_print(void);

//! \brief turn off printing of logs. can reduce output significantly
void malloc_extras_turn_off_print(void);

//! \brief get the pointer to the stolen heap
//! \return the heap pointer.
heap_t* malloc_extras_get_stolen_heap(void);

//! \brief stops a binary dead, 1 way or another
//! \param[in] result_code: to put in user 1
void malloc_extras_terminate(uint result_code);

//! \brief checks a pointer for safety stuff
//! \param[in] ptr: the malloc pointer to check for memory overwrites
//! \return true if nothing is broken, false if there was detected overwrites.
//! \rtype: bool
bool malloc_extras_check(void *ptr);

//! \brief allows the ability to read the size of a malloc.
//! \param[in] ptr: the pointer to get the size in words of.
//! \return returns the size of a given malloc in words.
int malloc_extras_malloc_size(void *ptr);

//! \brief checks a given pointer with a marker
//! \param[in] ptr: the pointer marker for whats being checked.
//! \param[in] marker: the numerical marker for this test. allowing easier
//! tracking of where this check was called in the user application code
//! (probably should be a string. but meh)
void malloc_extras_check_marked(void *ptr, int marker);

//! \brief checks all malloc's with a given marker. to allow easier tracking
//! from application code (probably should be a string. but meh)
//! \param[in] marker: the numerical marker for this test. allowing easier
//! tracking of where this check was called in the user application code
//! (probably should be a string. but meh)
void malloc_extras_check_all_marked(int marker);

//! \brief checks all malloc's for overwrites with no marker. This does not
//! \provide a easy marker to track back to the application user code.
void malloc_extras_check_all(void);

//! \brief stores a generated heap pointer. sets up trackers for this
//! core if asked. DOES NOT REBUILD THE FAKE HEAP!
//! \param[in] heap_location: address where heap is location
//! \return bool where true states the initialisation was successful or not
bool malloc_extras_initialise_with_fake_heap(
        heap_t *heap_location);

//! \brief builds a new heap based off stolen SDRAM blocks from cores
//! synaptic matrix's. Needs to merge in the true SDRAM free heap, as
//! otherwise its impossible to free the block properly.
//! \param[in] sizes_region; the SDRAM address where the free regions exist
//! \return None
bool malloc_extras_initialise_fake_heap(
        available_sdram_blocks *sizes_region);

//! \brief builds a new heap with no stolen SDRAM and sets up the malloc
//! tracker.
//! \return bool where true is a successful initialisation and false otherwise.
bool malloc_extras_initialise_no_fake_heap_data(void);

//! \brief frees the SDRAM allocated from whatever heap it came from
//! \param[in] ptr: the address to free. could be DTCM or SDRAM
void malloc_extras_safe_x_free_marked(void *ptr, int marker);

//! \brief frees a pointer without any marker for application code
//! \param[in] ptr: the pointer to free.
void malloc_extras_safe_x_free(void *ptr);

//! \brief mallocs a number of bytes from SDRAM. If safety turned on, it
//! allocates more SDRAM to support buffers and size recordings.
//! \parma[in] bytes: the number of bytes to allocate from SDRAM.
//! \return the pointer to the location in SDRAM to use in application code.
void * malloc_extras_safe_sdram_malloc_wrapper(uint bytes);

//! \brief allows a search of the 2 heaps available. (DTCM, stolen SDRAM)
//! NOTE: commented out as this can cause stack overflow issues quickly.
//! if deemed safe. could be uncommented out. which the same to the #define
//! below at the end of the file
//! \param[in] bytes: the number of bytes to allocate.
//! \return: the address of the block of memory to utilise.
void * safe_malloc(uint bytes);

#define MALLOC safe_malloc
#define FREE   malloc_extras_safe_x_free
#define FREE_MARKED malloc_extras_safe_x_free_marked
#define MALLOC_SDRAM malloc_extras_safe_sdram_malloc_wrapper

#endif  // __MALLOC_EXTRAS_H__
