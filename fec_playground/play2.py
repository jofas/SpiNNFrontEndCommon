import logging
import json
import os
import shutil
from spinn_machine import Router
from pacman.model.routing_tables.multicast_routing_tables import (
    from_json, to_json)
from pacman.model.routing_tables import (MulticastRoutingTables)
from pacman.executor import PACMANAlgorithmExecutor
from pacman.utilities.json_utils import graphs_from_json, n_keys_map_from_json
from spinn_front_end_common.utilities.function_list import (
    get_front_end_common_pacman_xml_paths)


def clear_output(output_dir):
    tables_dir = os.path.join(output_dir, "routing_tables_from_machine")
    if os.path.isdir(tables_dir):
        shutil.rmtree(tables_dir)


def get_path(local, json_dir):
    if json_dir:
        full = os.path.join(json_dir, local)
    else:
        full = local
    if os.path.isfile(full):
        return full
    full = full + ".gz"
    if os.path.isfile(full):
        return full
    raise Exception("unable to find {}", local)


output_dir = "output"
clear_output(output_dir)
json_dir = "D:\spinnaker\my_spinnaker\peter"

"""
original_tables = from_json(get_path("routing_tables.json", json_dir))
big = MulticastRoutingTables()
for original in original_tables:
    print("x:", original.x," y:", original.y," len:",original.number_of_entries)
    if original.x == 40 and original.y == 59:
         big.add_routing_table(original)

    json_obj = to_json(big)
    # dump to json file
    with open("routing_table_40_59.json", "w") as f:
        json.dump(json_obj, f)
"""
tables = from_json("routing_table_40_59.json")
convert = Router.convert_routing_table_entry_to_spinnaker_route
for table in tables:
    entries = table.number_of_entries
    defaultable = table.number_of_defaultable_entries
    link_only = 0
    spinnaker_routes = set()
    for entry in table.multicast_routing_entries:
        if not entry.processor_ids:
            link_only += 1
        spinnaker_routes.add(convert(entry))
    print("{} entries of which {} are "
    "defaultable and {} link only with {} unique "
    "spinnaker routes\n"
    "".format(entries, defaultable, link_only, len(spinnaker_routes)))
