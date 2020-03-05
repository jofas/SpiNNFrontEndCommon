import json
import logging
import os
import shutil
import tempfile
from pacman.executor import PACMANAlgorithmExecutor
from pacman.utilities.json_utils import graphs_from_json, n_keys_map_from_json, graphs_to_json, partition_to_n_keys_map_to_json
from spinn_front_end_common.utilities.function_list import (
    get_front_end_common_pacman_xml_paths)


def get_machine_inputs(n_boards):
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
            "NBoardsRequired": n_boards
        }
        algorithms = ["SpallocAllocator"]
    return inputs, algorithms


def get_json_machine_inputs(json_dir):
    inputs = {
        #"MachineHeight": None,
        #"MachineWidth": None,
        "MachineJsonPath": get_path("machine.json", json_dir)
    }
    algorithms = ["VirtualMachineGenerator"]
    return inputs, algorithms


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


def graph_stats(graph):
    print("vertexes:", graph.n_vertices)
    print("partitions:", graph.n_outgoing_edge_partitions)
    no_outgoing_edge = []
    no_incoming_edge = []
    max_incoming = 0
    n_edges = 0
    incoming_counts = {}
    for vertex in graph.vertices:
        outgoing = graph.get_edges_starting_at_vertex(vertex)
        if len(outgoing) == 0:
            no_outgoing_edge.append(vertex)
        else:
            n_edges += len(outgoing)
        incoming = graph.get_edges_ending_at_vertex(vertex)
        if len(incoming) == 0:
            no_incoming_edge.append(vertex)
        else:
            inputs = len(incoming)
            max_incoming = max(max_incoming, inputs)
            incoming_counts[inputs] = incoming_counts.get(inputs, 0) + 1
    print(n_edges, " edges")
    print(max_incoming, "max_incoming")
    print("No outgoing", len(no_outgoing_edge))
    for count in sorted(incoming_counts.keys()):
        print(count, incoming_counts[count])
    #for vertex in no_outgoing_edge:
    #        print(vertex)
    print("No incoming", len(no_incoming_edge))
    #for vertex in no_incoming_edge:
    #        print(vertex)


def mapper_stats(application_graph, graph_mapper):
    for vertex in application_graph.vertices:
        print(vertex, len(graph_mapper.get_machine_vertices(vertex)))


logging.basicConfig(level=logging.INFO)

#temp = tempfile.mkdtemp()
output_dir = "output"
clear_output(output_dir)
json_dir = "D:\spinnaker\my_spinnaker\peter"

# machine_inputs, machine_algorithms = get_machine_inputs(100)
machine_inputs, machine_algorithms = get_json_machine_inputs(json_dir)

# graphs_path = get_path("graphs.json", json_dir)
graphs_path = "graphs_min.json"
print("reading ", graphs_path)
application_graph, machine_graph, graph_mapper = graphs_from_json(
    graphs_path)

print("app_vertexes:", application_graph.n_vertices)
graph_stats(machine_graph)
mapper_stats(application_graph, graph_mapper)

n_keys_map = n_keys_map_from_json(
#    get_path("n_keys_map.json", json_dir), machine_graph)
    "n_keys_map_min.json", machine_graph)


inputs = {
    "MemoryMachineGraph": machine_graph,
    "MemoryApplicationGraph": application_graph,
    "MemoryGraphMapper": graph_mapper,
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
    "SystemProvenanceFilePath": output_dir,
    "IPAddress": "bogus value for report to print"
}
inputs.update(machine_inputs)
algorithms = [
    #"RoutingSetup",
    #"MachineGenerator",
    "SpreaderPlacer",
    # "WriteJsonMachineGraph",
    "MallocBasedChipIDAllocator",
    "NerRoute",
    "BasicTagAllocator",
    "ProcessPartitionConstraints",
    "ZonedRoutingInfoAllocator",
    "ZonedRoutingTableGenerator",
    #"MundyOnChipRouterCompression",
    "CompressedRouterSummaryReport",
    "PairCompressor",
    #"routingInfoReports",
    #"RoutingTableFromMachineReport"
] + machine_algorithms

executor = PACMANAlgorithmExecutor(
    algorithms, [], inputs, [], [], [],
    xml_paths=get_front_end_common_pacman_xml_paths())
executor.execute_mapping()
