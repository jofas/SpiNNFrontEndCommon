import logging
import os
import shutil
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
json_dir = "/home/brenninc/spinnaker/my_spinnaker/peter"
#json_dir = "/home/brenninc/spinnaker/my_spinnaker/reports/2020-03-04-13-16-27-83692/run_1/json_files"

graphs_path = get_path("graphs.json", json_dir)

