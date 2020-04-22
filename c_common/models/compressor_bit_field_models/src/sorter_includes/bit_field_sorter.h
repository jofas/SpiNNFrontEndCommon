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

#ifndef __BIT_FIELD_SORTER_H__
#define __BIT_FIELD_SORTER_H__

#include "sorters.h"
#include <malloc_extras.h>

//! \brief reads a bitfield and deduces how many bits are not set
//! \param[in] filter_info_struct: the struct holding a bitfield
//! \param[in] the addresses of the regions to read
//! \return how many redundant packets there are
uint32_t detect_redundant_packet_count(
    filter_info_t filter_info_struct, region_addresses_t *region_addresses){
    uint32_t n_filtered_packets = 0;
    uint32_t n_neurons = helpful_functions_locate_key_atom_map(
        filter_info_struct.key, region_addresses);
    for (uint neuron_id = 0; neuron_id < n_neurons; neuron_id++) {
        if (!bit_field_test(filter_info_struct.data, neuron_id)) {
            n_filtered_packets += 1;
        }
    }
    log_debug("n filtered packets = %d", n_filtered_packets);
    return n_filtered_packets;
}

//! \brief do some location and addition stuff
//! \param[in] coverage:the set of bitfields and corresponding processors
//!                      for bitfields with a given redundant packet count.
//! \param[in] coverage_index: where in the coverage array we are
//! \param[in] cores_to_add_for: the cores who's bitfields we want to find
//! \param[in] cores_to_add_length: length of array of core ids
//! \param[in] diff: the amount of bitfields to add for these cores
//! \param[out] covered: the new set of bitfields
//! \param[out] sorted_bit_fields: the pointer to where sorted bitfields are
//! \param[in] sorted_bf_fill_loc: the location in the sorted bit fields
//! currently filling in
//! \param[in] region_addresses: the sdram where all regions are
//! \return the new covered level
static inline int locate_and_add_bit_fields(
        _coverage_t** coverage, int coverage_index,
        int *cores_to_add_for, int cores_to_add_length, int diff,
        int covered, sorted_bit_fields_t* sorted_bit_fields,
        int *sorted_bf_fill_loc, region_addresses_t *region_addresses){

    log_debug(
        "going to look for %d cores with a diff of %d",
        cores_to_add_length, diff);
    for (int index = 0; index < cores_to_add_length; index++) {
        log_debug("am allowed to add from core %d", cores_to_add_for[index]);
    }

    // get the coverage we're interested in
    _coverage_t* coverage_e = coverage[coverage_index];
    log_debug(
        "taking from coverage %d which has r packets of %d",
        coverage_index, coverage_e->n_redundant_packets);

    for (int p_index = 0; p_index < coverage_e->length_of_list; p_index++) {
        // check for the processor id's in the cores to add from, and add the
        // bitfield with that redundant packet rate and processor to the sorted
        // bitfields
        int proc = coverage_e->processor_ids[p_index];

        // look to see if the core is one of those allowed to merge
        for (int check_idx = 0; check_idx < cores_to_add_length; check_idx++) {
            int allowed_p = cores_to_add_for[check_idx];

            // escape when we've found enough to satisfy the diff
            if (covered >= diff) {
                return covered;
            }

            // if processor is one to take bf's from. remove and update.
            // Ensure we're not adding one which we've already added
            if (proc == allowed_p &&
                    coverage_e->bit_field_addresses[proc] != NULL) {
                // update coverage so that it can reflect merger
                covered += 1;

                // add to sorted bitfield
                sorted_bit_fields->bit_fields[*sorted_bf_fill_loc] =
                    coverage_e->bit_field_addresses[p_index];
                sorted_bit_fields->processor_ids[*sorted_bf_fill_loc] = proc;
                *sorted_bf_fill_loc += 1;

                log_debug(
                    "dumping into sorted at index %d proc %d, for key %d and "
                    "has redundant packet count of %d",
                    *sorted_bf_fill_loc - 1, proc,
                    coverage_e->bit_field_addresses[p_index]->key,
                    detect_redundant_packet_count(
                        *coverage_e->bit_field_addresses[p_index],
                        region_addresses));

                // delete (aka set to null, to bypass lots of data moves and
                // ensure the next time we know not to add this one)
                coverage_e->bit_field_addresses[p_index] = NULL;
                coverage_e->processor_ids[p_index] = NULL;

                log_debug(
                    "removing from index's %d, %d", coverage_index, p_index);
            }
        }
    }
    return covered;
}

//! \brief printer for the coverage struct bitfields component
//! \param[in] n_unique_redundant_packet_counts: how many coverage bits there
//!  are
//! \param[in] coverage: the coverage struct (pointer to a list of them).
static void print_coverage_for_sanity_purposes(
        int n_unique_redundant_packet_counts, _coverage_t **coverage){
    log_debug("printing coverage");
    int added = 0;
    for (int c_index = 0; c_index < n_unique_redundant_packet_counts;
            c_index++){
        for (int bf_index = 0; bf_index < coverage[c_index]->length_of_list;
                bf_index++){
            log_debug(
                "before sort by n bitfields bitfield address in coverage at "
                "index %d in array index %d is %x",
                c_index, bf_index,
                coverage[c_index]->bit_field_addresses[bf_index]);
            added += 1;
        }
    }
    log_debug("added %d bitfields", added);
}

//! \brief printer for the coverage struct processor component
//! \param[in] n_unique_redundant_packet_counts: how many coverage bits there
//!  are
//! \param[in] coverage: the coverage struct (pointer to a list of them).
void print_coverage_procs_for_sanity_purposes(
        int n_unique_redundant_packet_counts, _coverage_t **coverage){
     for (int c_index = 0; c_index < n_unique_redundant_packet_counts;
            c_index++){
        for (int bf_index = 0; bf_index < coverage[c_index]->length_of_list;
                bf_index ++){
            log_debug(
                "bitfield in coverage at index %d in bf index x %d is proc %d "
                " with redundant packet count %d",
                c_index, bf_index, coverage[c_index]->processor_ids[bf_index],
                coverage[c_index]->n_redundant_packets);
        }
    }
}

//! \brief takes whats left in the coverage and adds them to the sorted bit
//! fields
//! \param[in] sorted_bit_fields: the pointer to the sorted bitfield struct
//! \param[in] n_unique_redundant_packet_counts: how many coverage bits there
//!  are
//! \param[in] coverage: the coverage struct (pointer to a list of them).
//! \param[in] sorted_bf_fill_loc: the current position in the sorted
//! \param[in] region_addresses: the sdram of all the regions.
//! bitfields for filling in.
void add_left_overs(
        sorted_bit_fields_t* sorted_bit_fields,
        int n_unique_redundant_packet_counts, _coverage_t** coverage,
        int *sorted_bf_fill_loc, region_addresses_t *region_addresses) {

    // iterate through the coverage and add any that are left over.
    for (int i = 0; i < n_unique_redundant_packet_counts; i++) {
        for (int bf_index = 0; bf_index < coverage[i]->length_of_list;
                bf_index++) {
            if (coverage[i]->bit_field_addresses[bf_index] != NULL) {

                sorted_bit_fields->bit_fields[*sorted_bf_fill_loc] =
                    coverage[i]->bit_field_addresses[bf_index];

                log_debug(
                    "dumping into sorted at index %d proc %d, for key %d and "
                    "has redundant packet count of %d",
                    *sorted_bf_fill_loc, coverage[i]->processor_ids[bf_index],
                    coverage[i]->bit_field_addresses[bf_index]->key,
                    detect_redundant_packet_count(
                        *coverage[i]->bit_field_addresses[bf_index],
                        region_addresses));

                sorted_bit_fields->processor_ids[*sorted_bf_fill_loc] =
                    coverage[i]->processor_ids[bf_index];

                *sorted_bf_fill_loc += 1;
            }
        }
    }
}

//! \prints out the proc by coverage map
//! \param[in] n_pairs_of_addresses: the n pairs of addresses
//! \param[in] proc_cov_by_bf: the proc by coverage map
void print_proc_by_coverage(
        int n_pairs_of_addresses, _proc_cov_by_bitfield_t** proc_cov_by_bf) {
    uint32_t current_n_bit_fields = 0;
    for (int region_index = 0;
            region_index < n_pairs_of_addresses;
            region_index++) {
        for (int l_id =0;
                l_id < proc_cov_by_bf[region_index]->length_of_list;
                l_id++) {
            log_debug(
                "proc %d at index %d has redundant packets count of %d",
                proc_cov_by_bf[region_index]->processor_id, l_id,
                proc_cov_by_bf[region_index]->redundant_packets[l_id]);
            current_n_bit_fields += 1;
        }
    }
    log_debug("there were %d bitfields", current_n_bit_fields);
}

//! \brief adds the bitfields for the binary search based off the impact
//! made in reducing the redundant packet processing on cores.
//! \param[in] coverage: the set of bitfields and corresponding processors
//!                      for bitfields with a given redundant packet count.
//! \param[in] proc_cov_by_bit_field: the processors bitfield redundant
//! packet counts.
//! \param[in] n_pairs: the number of processors/elements to search
//! \param[in] n_unique_redundant_packet_counts: the count of how many unique
//!      redundant packet counts there are.
//! \param[in] sorted_bit_fields:  the pointer to the sorted bitfield struct
//! \param[in] region_addresses: the sdram of all the regions.
//! \return None
static inline void add_bit_fields_based_on_impact(
        _coverage_t **coverage,
        _proc_cov_by_bitfield_t **proc_cov_by_bit_field,
        int n_pairs, int n_unique_redundant_packet_counts,
        sorted_bit_fields_t* sorted_bit_fields,
        region_addresses_t *region_addresses) {

    // print all coverage for sanity purposes
    print_coverage_for_sanity_purposes(
        n_unique_redundant_packet_counts, coverage);

    // sort processor coverage by bitfield so that ones with longest length are
    // at the front of the list
    sorter_sort_by_n_bit_fields(proc_cov_by_bit_field, n_pairs);

    // print all coverage for sanity purposes
    //print_coverage_procs_for_sanity_purposes(
    //    n_unique_redundant_packet_counts, coverage);

    // move bit_fields over from the worst affected cores. The list of worst
    // affected cores will grow in time as the worst cores are balanced out
    // by the redundant packets being filtered by each added bitfield.
    int cores_to_add_for[n_pairs];
    int cores_to_add_length = 0;
    int sorted_bf_fill_loc = 0;

    // go through all cores but last 1
    for (int worst_core_id = 0; worst_core_id < n_pairs - 1;
            worst_core_id++){

        // add worst core to set to look for bitfields in
        cores_to_add_for[cores_to_add_length] =
            proc_cov_by_bit_field[worst_core_id]->processor_id;
        cores_to_add_length += 1;
        log_debug(
            "adding core %d into the search",
            proc_cov_by_bit_field[worst_core_id]->processor_id);

        // determine difference between the worst and next worst
        log_debug(
            "worst has %d bitfields, worst +1 has %d bitfields",
            proc_cov_by_bit_field[worst_core_id]->length_of_list,
            proc_cov_by_bit_field[worst_core_id + 1]->length_of_list);
        int diff = proc_cov_by_bit_field[worst_core_id]->length_of_list -
             proc_cov_by_bit_field[worst_core_id + 1]->length_of_list;
        log_debug("diff is %d", diff);

        // sort by bubble sort so that the most redundant packet count
        // addresses are at the front
        sorter_sort_by_redundant_packet_count(
            proc_cov_by_bit_field,
            proc_cov_by_bit_field[worst_core_id]->length_of_list,
            worst_core_id);

        print_proc_by_coverage(
            region_addresses->n_pairs, proc_cov_by_bit_field);

        // print for sanity
        for (int r_packet_index = 0;
                r_packet_index < proc_cov_by_bit_field[
                    worst_core_id]->length_of_list;
                r_packet_index ++){
            log_debug(
                "order of redundant packet count at index %d is %d",
                r_packet_index,
                proc_cov_by_bit_field[worst_core_id]->redundant_packets[
                    r_packet_index]);
        }

        //print_coverage_for_sanity_purposes(
        //    n_unique_redundant_packet_counts, coverage);

        // cycle through the list of a cores redundant packet counts and locate
        // the bitfields which match up
        int covered = 0;
        for (int redundant_packet_count_index = 0;
                redundant_packet_count_index <
                proc_cov_by_bit_field[worst_core_id]->length_of_list;
                redundant_packet_count_index ++){

            // the coverage packet count to try this time
            int x_redundant_packets = proc_cov_by_bit_field[
                worst_core_id]->redundant_packets[redundant_packet_count_index];

            // locate the bitfield with coverage that matches the x redundant
            // packets and add them to the sorted struct
            for (int i = 0; i < n_unique_redundant_packet_counts; i++) {
                if (coverage[i]->n_redundant_packets == x_redundant_packets) {
                    covered = locate_and_add_bit_fields(
                        coverage, i, cores_to_add_for, cores_to_add_length,
                        diff, covered, sorted_bit_fields, &sorted_bf_fill_loc,
                        region_addresses);
                    log_debug("filled sorted to %d", sorted_bf_fill_loc);
                }
            }

            // print all coverage for sanity purposes
            //print_coverage_for_sanity_purposes(
            //    n_unique_redundant_packet_counts, coverage);
            //print_coverage_procs_for_sanity_purposes(
            //    n_unique_redundant_packet_counts, coverage);
            log_debug("next cycle of moving to sorted");
        }
    }

    // print all coverage for sanity purposes
    print_coverage_for_sanity_purposes(
        n_unique_redundant_packet_counts, coverage);

    // add left overs
    sorter_sort_bitfields_so_most_impact_at_front(
        coverage, n_unique_redundant_packet_counts);
    add_left_overs(
        sorted_bit_fields, n_unique_redundant_packet_counts, coverage,
        &sorted_bf_fill_loc, region_addresses);
    //log_debug("filled sorted to %d", sorted_bf_fill_loc);
}

//! \brief creates a struct that defines which bitfields are based on which
//! redundant packet count of those bitfields.
//! \param[in] region_addresses: the location of all the regions
//! \return the proc by coverage struct.
static inline _proc_cov_by_bitfield_t** create_coverage_by_bit_field(
        region_addresses_t *region_addresses){

    int n_pairs_of_addresses = region_addresses->n_pairs;

    // build processor coverage by bitfield
    _proc_cov_by_bitfield_t **proc_cov_by_bf = MALLOC(
        n_pairs_of_addresses * sizeof(_proc_cov_by_bitfield_t*));
    if (proc_cov_by_bf == NULL) {
        log_error("failed to allocate memory for processor coverage by "
                  "bitfield, if it fails here. might as well give up");
        return NULL;
    }
    log_debug("finished malloc proc_cov_by_bf");

    // iterate through a processors bitfield region and get n bitfields
    for (int region_index = 0; region_index < n_pairs_of_addresses;
            region_index++) {

        // malloc for n redundant packets
        proc_cov_by_bf[region_index] = MALLOC(sizeof(_proc_cov_by_bitfield_t));
        if (proc_cov_by_bf[region_index] == NULL){
            log_error("failed to allocate memory for processor coverage for "
                      "region %d. might as well give up", region_index);
            return NULL;
        }

        // track processor id
        log_debug("put processor in proc cov");
        proc_cov_by_bf[region_index]->processor_id =
            region_addresses->pairs[region_index].processor;

        // track lengths
        filter_region_t *filter_region =
            region_addresses->pairs[region_index].filter;
        uint32_t core_n_bit_fields = filter_region->n_filters;
        proc_cov_by_bf[region_index]->length_of_list = core_n_bit_fields;

        // malloc for n redundant packets
        if (core_n_bit_fields != 0) {
            proc_cov_by_bf[region_index]->redundant_packets =
                MALLOC(core_n_bit_fields * sizeof(int));
            if (proc_cov_by_bf[region_index]->redundant_packets == NULL){
                log_error(
                    "failed to allocate memory of %d for processor coverage "
                    "for region %d, might as well fail",
                    core_n_bit_fields * sizeof(int), region_index);
                return NULL;
            }
        }

        log_debug("fill in redudnant packets");
        for (uint32_t bf_id = 0; bf_id < core_n_bit_fields; bf_id++){
            uint32_t n_red_packets = detect_redundant_packet_count(
                filter_region->filters[bf_id], region_addresses);
            proc_cov_by_bf[region_index]->redundant_packets[bf_id] = n_red_packets;
        }
    }

    print_proc_by_coverage(n_pairs_of_addresses, proc_cov_by_bf);

    return proc_cov_by_bf;
}

//! \brief checks if a redundant packet count is already in the list of
//! redundant packet counts.
//! \param[in] length_n_redundant_packets: how many unique redundant packet
//! counts have been found already.
//! \param[in] unique_redundant_packets: list of unique redundant packet counts.
//! \param[in] redundant_packet_count: the new unique redundant packet count
//! to try to find in the current set.
//! \return bool that states true if the new redundant packet count has been
//! found already, false otherwise.
static bool is_already_found(
        int length_n_redundant_packets, int *unique_redundant_packets,
        int redundant_packet_count) {
    for (int index = 0; index < length_n_redundant_packets; index++) {
        if (unique_redundant_packets[index] == redundant_packet_count) {
            return true;
        }
    }
    return false;
}

//! \brief locate all the counts of redundant packets from every bitfield and
//! find the total unique counts and return how of them there are.
//! \param[in] region_addresses: the location of all the regions
//! \param[in] proc_cov_by_bf: the struct holding coverage per processor
//! \param[in] unique_redundant_packets: the list of redundant packet count.
//! \return the number of unique redundant packets.
static inline int determine_unique_redundant_packets(
        region_addresses_t *region_addresses,
        _proc_cov_by_bitfield_t **proc_cov_by_bf,
        int *unique_redundant_packets) {

    int unique_packet_count_index = 0;
    int n_pairs_of_addresses = region_addresses->n_pairs;

    // filter out duplicates in the n redundant packets
    for (int region_index = 0; region_index < n_pairs_of_addresses;
            region_index++) {
        // cycle through the bitfield registers again to get n bitfields per
        // core
        filter_region_t *filter_region =
            region_addresses->pairs[region_index].filter;
        int core_n_filters = filter_region->n_filters;

        // check that each bitfield redundant packets are unique and add to set
        for (int bf_id = 0; bf_id < core_n_filters;  bf_id++) {
            int redundant_packet_count =
                proc_cov_by_bf[region_index]->redundant_packets[bf_id];

            // if not a duplicate, add to list and update size
            if (!is_already_found(
                    unique_packet_count_index, unique_redundant_packets,
                    redundant_packet_count)) {
                unique_redundant_packets[unique_packet_count_index] =
                    redundant_packet_count;
                log_debug(
                    "putting %d in unique index %d",
                    redundant_packet_count, unique_packet_count_index);
                unique_packet_count_index += 1;
            }
        }
    }

    for (int index = 0; index < unique_packet_count_index; index ++) {
        log_debug(
            "redundant packet count at index %d is %d",
            index, unique_redundant_packets[index]);
    }

    log_debug("length of n redundant packets = %d", unique_packet_count_index);
    return unique_packet_count_index;
}

//! \brief creates a map of bitfields which have the same redundant packet count
//! \param[in] n_unique_redundant_packets: the number of unique redundant
//! packet counts.
//! \param[in] unique_redundant_packets: the list of redundant packets counts
//! \param[in] n_pairs_of_addresses: the number of addresses to search through
//! \param[in] proc_cov_by_bf: the map of processor to redundant packet count
//! \param[in] bf_by_processor: the map from processor to bitfields.
//! \return list of coverage structs.
static inline _coverage_t** create_coverage_by_redundant_packet(
        int n_unique_redundant_packets, int* unique_redundant_packets,
        int n_pairs_of_addresses, _proc_cov_by_bitfield_t** proc_cov_by_bf,
        bit_field_by_processor_t* bf_by_processor){

    // malloc space for the bitfield by coverage map
    int size = n_unique_redundant_packets * sizeof(_coverage_t*);
    log_debug(
        "size for %d unique redundant packets is %d",
        n_unique_redundant_packets, size);
    
    // malloc coverage
    _coverage_t** coverage =
        MALLOC_SDRAM(n_unique_redundant_packets * sizeof(_coverage_t*));
    if (coverage == NULL) {
        log_error("failed to malloc memory for the bitfields by coverage. "
                  "might as well fail");
        return NULL;
    }

    log_debug("the address for the coverage main pointer is %x", &coverage);

    for (int unique_redundant_index = 0;
            unique_redundant_index < n_unique_redundant_packets;
            unique_redundant_index++) {
        coverage[unique_redundant_index] = NULL;
    }

    bool passed = malloc_extras_check(coverage);
    if (! passed){
        log_error("failed");
        malloc_extras_terminate(DETECTED_MALLOC_FAILURE);
    }

    // go through the unique x redundant packets and build the list of
    // bitfields for it
    for (int unique_redundant_index = 0;
            unique_redundant_index < n_unique_redundant_packets;
            unique_redundant_index++) {
        // malloc a redundant packet entry
        log_debug(
            "try to allocate memory of size %d for coverage at index %d",
             sizeof(_coverage_t), unique_redundant_index);
        _coverage_t *element = MALLOC(sizeof(_coverage_t));

        // update the redundant packet pointer
        passed = malloc_extras_check(coverage);
        if (! passed){
            log_error("failed");
            malloc_extras_terminate(EXIT_MALLOC);
        }

        coverage[unique_redundant_index] = element;
        if (coverage[unique_redundant_index] == NULL) {
            log_error(
                "failed to malloc memory for the bitfields by coverage "
                "for index %d. might as well fail", unique_redundant_index);
            return NULL;
        }

        // update the redundant packet pointer
        malloc_extras_check_all();
        passed = malloc_extras_check(coverage);
        if (! passed){
            log_error("failed");
            malloc_extras_terminate(EXIT_MALLOC);
        }

        // update the number of redundant packets for this coverage
        coverage[unique_redundant_index]->n_redundant_packets =
            unique_redundant_packets[unique_redundant_index];
        passed = malloc_extras_check(coverage);
        if (! passed){
            log_error("failed");
            malloc_extras_terminate(EXIT_MALLOC);
        }

        // search to see how long the list is going to be.
        int n_bf_with_same_r_packets = 0;
        for (int region_index = 0; region_index < n_pairs_of_addresses;
                region_index++) {

            int length = proc_cov_by_bf[region_index]->length_of_list;
            for (int redundent_index = 0;
                    redundent_index < length;
                    redundent_index ++) {
                if (proc_cov_by_bf[region_index]->redundant_packets[redundent_index]
                        == unique_redundant_packets[unique_redundant_index]) {
                    n_bf_with_same_r_packets += 1;
                }
            }
        }

        log_debug("size going to be %d", n_bf_with_same_r_packets);

        // update length of list
        coverage[unique_redundant_index]->length_of_list =
            n_bf_with_same_r_packets;

        passed = malloc_extras_check(coverage);
        if (! passed){
            log_error("failed");
            malloc_extras_terminate(EXIT_MALLOC);
        }

        // malloc list size for these addresses of bitfields with same
        // redundant packet counts.
        coverage[unique_redundant_index]->bit_field_addresses =
            MALLOC((n_bf_with_same_r_packets) * sizeof(filter_info_t*));
        if (coverage[unique_redundant_index]->bit_field_addresses == NULL) {
            log_error(
                "failed to allocate memory for the coverage on index %d"
                " for addresses. might as well fail.", unique_redundant_index);
            return NULL;
        }

        // malloc list size for the corresponding processors ids for the
        // bitfields
        log_debug(
            "trying to allocate %d bytes, for %d bitfields with same "
            "xr packets",
            n_bf_with_same_r_packets * sizeof(uint32_t),
            n_bf_with_same_r_packets);
        coverage[unique_redundant_index]->processor_ids =
            MALLOC((n_bf_with_same_r_packets) * sizeof(uint32_t));
        if (coverage[unique_redundant_index]->processor_ids == NULL) {
            log_error(
                "failed to allocate memory for the coverage on index %d"
                " for processors. might as well fail.", unique_redundant_index);
            return NULL;
        }

        // populate list of bitfields addresses which have same redundant
        //packet count.
        log_debug(
            "populating list of bitfield addresses with same packet count");
        int processor_i = 0;
        for (int region_index = 0; region_index < n_pairs_of_addresses;
                region_index++) {

            log_debug(
                "length of list = %d",
                proc_cov_by_bf[region_index]->length_of_list);
            log_debug("processor_i = %d", processor_i);

            for (int proc_redundant_index = 0;
                    proc_redundant_index <
                        proc_cov_by_bf[region_index]->length_of_list;
                    proc_redundant_index ++) {

                log_debug(
                    "comparing %d with %d",
                    proc_cov_by_bf[region_index]->redundant_packets[
                        proc_redundant_index],
                    unique_redundant_packets[unique_redundant_index]);
                log_debug("processor_i = %d", processor_i);

                if (proc_cov_by_bf[region_index]->redundant_packets[
                        proc_redundant_index] ==
                            unique_redundant_packets[unique_redundant_index]) {

                    log_debug(
                        "found! at %x",
                        bf_by_processor[region_index].bit_field_addresses[
                            proc_redundant_index]);
                    log_debug("processor_i = %d", processor_i);

                    coverage[unique_redundant_index]->bit_field_addresses[
                        processor_i] = &bf_by_processor[
                            region_index].bit_field_addresses[
                                proc_redundant_index];

                    coverage[unique_redundant_index]->processor_ids[
                        processor_i] = bf_by_processor[
                            region_index].processor_id;

                    processor_i += 1;
                    log_debug("updated processor_i to %d", processor_i);

                    passed = malloc_extras_check(coverage);
                    if (! passed){
                        log_error("failed");
                        malloc_extras_terminate(EXIT_MALLOC);
                    }
                    passed = malloc_extras_check(
                        coverage[unique_redundant_index]->processor_ids);
                    if (! passed){
                        log_error("failed");
                        malloc_extras_terminate(EXIT_MALLOC);
                    }
                    passed = malloc_extras_check(
                        coverage[unique_redundant_index]->bit_field_addresses);
                    if (! passed){
                        log_error("failed");
                        malloc_extras_terminate(EXIT_MALLOC);
                    }
                }
            }
        }
        log_debug(
            "processor id index = %d and need to fill in %d elements",
            processor_i, n_bf_with_same_r_packets);
        if (processor_i != n_bf_with_same_r_packets){
            log_error("WTF!");
            rt_error(RTE_SWERR);
        }
    }
    return coverage;
}


//! \brief no sorting, just plonking into list sorted bitfield list.
//! \param[in] region_addresses: the sdram that stores data addresses
//! \param[in] sorted_bit_fields: the sorted bitfields struct pointer
//! \param[in] bit_field_by_processor: the map of processor to bitfields.
void just_add_to_list(
        region_addresses_t *region_addresses,
        sorted_bit_fields_t* sorted_bit_fields,
        bit_field_by_processor_t* bit_field_by_processor){
    use(bit_field_by_processor);

    int pos_in_sorted = 0;
    int n_regions = region_addresses->n_pairs;
    log_debug("n regions is %d", n_regions);
    for (int region_index = 0; region_index < n_regions; region_index++){
        filter_region_t *filter_region =
            region_addresses->pairs[region_index].filter;
        int core_n_bit_fields = filter_region->n_filters;
        log_debug("n bitfields in region %d is %d", region_index, core_n_bit_fields);

        for (int bf_id = 0; bf_id < core_n_bit_fields; bf_id++){
            sorted_bit_fields->bit_fields[pos_in_sorted] =
                &filter_region->filters[bf_id];
            sorted_bit_fields->processor_ids[pos_in_sorted] =
                region_addresses->pairs[region_index].processor;
            //print_bit_field_struct(&filter_region->filters[bf_id]);

            pos_in_sorted += 1;
        }
    }
}


//! \brief reads in bitfields, makes a few maps, sorts into most priority.
//! \param[in] n_bf_addresses: the number of bitfields to sort
//! \param[in] region_addresses: the addresses where the data is
//! \param[in] bit_field_by_processor: the map from processor to bitfields
//! \return bool that states if it succeeded or not.
sorted_bit_fields_t* bit_field_sorter_sort(
        int n_bf_addresses, region_addresses_t *region_addresses,
        bit_field_by_processor_t* bit_field_by_processor){

    sorted_bit_fields_t* sorted_bit_fields =
       MALLOC(sizeof(sorted_bit_fields_t));

    if (sorted_bit_fields == NULL){
        log_error("failed to allocate dtcm for sorted bitfields.");
        return NULL;
    }

    // malloc the separate bits of the sorted bitfield struct
    log_debug("n bitfield addresses = %d", n_bf_addresses);
    sorted_bit_fields->bit_fields = MALLOC(
        n_bf_addresses * sizeof(filter_info_t*));
    if (sorted_bit_fields->bit_fields == NULL){
        log_error("cannot allocate memory for the sorted bitfield addresses");
        return NULL;
    }

    // set to NULL for safety purposes
    log_debug("filling in bit-field index");
    for (int index = 0; index < n_bf_addresses; index++) {
        sorted_bit_fields->bit_fields[index] = NULL;
    }

    sorted_bit_fields->processor_ids =
        MALLOC(n_bf_addresses * sizeof(uint32_t));
    if (sorted_bit_fields->processor_ids == NULL){
        log_error("cannot allocate memory for the sorted bitfields with "
                  "processors ids");
        return NULL;
    }

    // NOTE: THis is if you want to debug / need surplus itcm. as this will
    //put the fields in the list, but without any sorting at all.
    //log_debug("just adding");
    just_add_to_list(
        region_addresses, sorted_bit_fields, bit_field_by_processor);
    log_debug("fin just adding");
    return sorted_bit_fields;

    // populate the bitfield by coverage
    log_debug("create proc cov by bitfield");
    _proc_cov_by_bitfield_t** proc_cov_by_bf = create_coverage_by_bit_field(
        region_addresses);
    if (proc_cov_by_bf == NULL){
        log_error("failed to allocate proc cov by bf.");
        return NULL;
    }

    // set up redundant packet tracker
    int* unique_redundant_packets = MALLOC(n_bf_addresses * sizeof(int));
    if (unique_redundant_packets == NULL){
        log_error("cannot allocate memory for the redundant packet counts");
        return NULL;
    }
    for (int index = 0; index < n_bf_addresses; index ++){
        unique_redundant_packets[index] = 0;
    }

    // determine how many unique redundant packets there are
    int n_unique_redundant_packets = determine_unique_redundant_packets(
        region_addresses, proc_cov_by_bf, unique_redundant_packets);

    // create coverage by redundant packets
    int n_pairs_of_addresses = region_addresses->n_pairs;
    log_debug("create coverage");
    _coverage_t** coverage = create_coverage_by_redundant_packet(
        n_unique_redundant_packets, unique_redundant_packets,
        n_pairs_of_addresses, proc_cov_by_bf, bit_field_by_processor);

    // free the redundant packet tracker, as now tailored ones are in the dict
    log_debug("freeing unique_redundant_packets");
    FREE(unique_redundant_packets);
    log_debug("freed unique_redundant_packets");

    // order the bitfields based off the impact to cores redundant packet
    // processing
    log_debug("adding bitfields based off impact");
    add_bit_fields_based_on_impact(
        coverage, proc_cov_by_bf, n_pairs_of_addresses,
        n_unique_redundant_packets, sorted_bit_fields, region_addresses);
    log_debug("added bitfields based off impact");

    // free the data holders we don't care about now that we've got our
    // sorted bitfields list
    for (int unique_redundant_index = 0;
            unique_redundant_index < n_unique_redundant_packets;
            unique_redundant_index++) {
        _coverage_t* cov_element = coverage[unique_redundant_index];
        FREE(cov_element->bit_field_addresses);
        cov_element->bit_field_addresses = NULL;
        FREE(cov_element->processor_ids);
        cov_element->processor_ids = NULL;
        FREE(cov_element);
        cov_element = NULL;
    }
    log_debug("free part 1");

    for (int region_index = 0;
            region_index < n_pairs_of_addresses;
            region_index++) {

        _proc_cov_by_bitfield_t* proc_cov_element =
            proc_cov_by_bf[region_index];

        if (proc_cov_element->length_of_list != 0) {
            FREE(proc_cov_element->redundant_packets);
            proc_cov_element->redundant_packets = NULL;
        }
        FREE(proc_cov_element);
        proc_cov_element = NULL;

    }
    log_debug("free part 2");
    FREE(coverage);
    coverage = NULL;
    log_debug("free part 3");
    FREE(proc_cov_by_bf);
    proc_cov_by_bf = NULL;
    log_debug("free part 4");
    return sorted_bit_fields;

}

#endif  // __BIT_FIELD_SORTER_H__
