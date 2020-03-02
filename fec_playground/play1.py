import logging
import os
import shutil
import tempfile
from pacman.executor import PACMANAlgorithmExecutor
from pacman.utilities.json_utils import graphs_from_json, n_keys_map_from_json
from spinn_front_end_common.utilities.function_list import (
    get_front_end_common_pacman_xml_paths)


def get_machine_inputs(n_boards):
    algorithms = []
    if n_boards is None:  # 4 chip
        inputs = {
            "IPAddress": "192.168.240.253",
            "BoardVersion": 3,
            "BMPDetails": None,
            "ResetMachineOnStartupFlag": False,
            "AutoDetectBMPFlag": False,
            "ScampConnectionData": None,
            "BootPortNum": None
        }
        algorithms = []
    else:
        inputs = {
            "SpallocServer": "spinnaker.cs.man.ac.uk",
            "SpallocPort": 22244,
            "SpallocUser": "Json_playground@manchester.ac.uk",
            "NBoardsRequired": 1
        }
        algorithms = ["SpallocAllocator"]
    return inputs, algorithms


def clear_output(output_dir):
    tables_dir = os.path.join(output_dir, "routing_tables_from_machine")
    if os.path.isdir(tables_dir):
        shutil.rmtree(tables_dir)


def read_graphs_from_json(json_dir=None):
    graph_json = "graph.json"
    key_json = "n_keys_map.json"
    if json_dir:
        graph_json = os.path.join(json_dir, graph_json)
        key_json = os.path.join(json_dir, key_json)
    application_graph, machine_graph, graph_mapper = graphs_from_json(
        graph_json)
    n_keys_map = n_keys_map_from_json(key_json, machine_graph)
    return application_graph, machine_graph, graph_mapper, n_keys_map


logging.basicConfig(level=logging.INFO)
machine_inputs, machine_algorithms = get_machine_inputs(None)

#temp = tempfile.mkdtemp()
output_dir = "output"
clear_output(output_dir)
application_graph, machine_graph, graph_mapper, n_keys_map = \
    read_graphs_from_json()

inputs = {
    "MemoryMachineGraph": machine_graph,
    "MemoryMachinePartitionNKeysMap": n_keys_map,
    "PlanNTimeSteps": 100,
    "JsonFolder": output_dir,
    "DownedChipsDetails": set(),
    "DownedCoresDetails": set(),
    "DownedLinksDetails": set(),
    "MaxSDRAMSize": None,
    "RepairMachine": False,
    "IgnoreBadEthernets": False,
    "ReportFolder": output_dir,
    "APPID": 123,
    "SystemProvenanceFilePath": output_dir
}
inputs.update(machine_inputs)
algorithms = [
    "RoutingSetup",
    "MachineGenerator",
    "SpreaderPlacer",
    # "WriteJsonMachineGraph",
    "MallocBasedChipIDAllocator",
    "NerRoute",
    "BasicTagAllocator",
    "ProcessPartitionConstraints",
    "MallocBasedRoutingInfoAllocator",
    "BasicRoutingTableGenerator",
    "MundyOnChipRouterCompression",
    "CompressedRouterSummaryReport",
    "routingInfoReports",
    "RoutingTableFromMachineReport"
] + machine_algorithms
executor = PACMANAlgorithmExecutor(
    algorithms, [], inputs, [], [], [],
    xml_paths=get_front_end_common_pacman_xml_paths())
executor.execute_mapping()
