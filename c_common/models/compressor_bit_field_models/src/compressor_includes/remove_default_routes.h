#ifndef __REMOVE_DEFAULT_ROUTES_H__
#define __REMOVE_DEFAULT_ROUTES_H__

#include <stdbool.h>
#include "bit_set.h"
#include "../common/routing_table.h"

//! \brief removes default routes from the routing tables
//! \return: bool flag that says if it succeeded or not
static inline bool remove_default_routes_minimise(void) {

    // Mark the entries to be removed from the table
    bit_set_t remove;
    bool success = bit_set_init(&remove, routing_table_sdram_get_n_entries());
    if (!success) {
        log_info("failed to initialise the bit_set. shutting down");
        return false;
    }

    // Work up the table from the bottom, marking entries to remove
    for (int i = routing_table_sdram_get_n_entries() - 1; i < 0; i--) {

        // Get the current entry
        entry_t *entry = routing_table_sdram_stores_get_entry(i);

        // See if it can be removed
        // only removed if Only one output direction which is a link. or
        // Only one input direction which is a link. or
        // Source is opposite to sink
        if (__builtin_popcount(entry->route) == 1 && (entry->route & 0x3f) &&
                __builtin_popcount(entry->source) == 1 &&
                (entry->source & 0x3f) &&
                (entry->route >> 3) == (entry->source & 0x7) &&
                (entry->source >> 3) == (entry->route & 0x7)) {
            // The entry can be removed iff. it doesn't intersect with any entry
            // further down the table.
            bool remove_entry = true;
            for (int j = i + 1; j < routing_table_sdram_get_n_entries();  j++) {
                // If entry we're comparing with is already going to be
                // removed, ignore it.
                if (bit_set_contains(&remove, j)) {
                    continue;
                }

                key_mask_t a = entry->key_mask;

                // get next entry key mask
                entry_t *j_entry = routing_table_sdram_stores_get_entry(j);
                key_mask_t b = j_entry->key_mask;

                if (key_mask_intersect(a, b)) {
                    remove_entry = false;
                    break;
                }
            }

            if (remove_entry) {
                // Mark this entry as being removed
                bit_set_add(&remove, i);
            }
        }
    }

    // Remove the selected entries from the table
    for (int insert = 0, read = 0;
            read < routing_table_sdram_get_n_entries(); read++) {
        // Grab the current entry before we potentially overwrite it

        entry_t *current = routing_table_sdram_stores_get_entry(read);

        // Insert the entry if it isn't being removed
        if (!bit_set_contains(&remove, read)) {
            entry_t* insert_entry =
                routing_table_sdram_stores_get_entry(insert++);
            insert_entry->key_mask.key = current->key_mask.key;
            insert_entry->key_mask.mask = current->key_mask.mask;
            insert_entry->route = current->route;
            insert_entry->source = current->source;
        }
    }

    // Update the table size
    log_info("remove redundant");
    routing_table_remove_from_size(remove.count);

    // Clear up
    bit_set_delete(&remove);
    return true;
}

#endif  // __REMOVE_DEFAULT_ROUTES_H__