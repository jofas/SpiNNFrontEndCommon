import tempfile
from pacman.executor import PACMANAlgorithmExecutor
from pacman.utilities.json_utils import graph_from_json, n_keys_map_from_json
from spinn_front_end_common.utilities.function_list import (
    get_front_end_common_pacman_xml_paths)

machine_graph = graph_from_json("machine_graph.json")
n_keys_map = n_keys_map_from_json("n_keys_map.json", machine_graph)

temp = tempfile.mkdtemp()
print("ApplicationDataFolder = {}".format(temp))
inputs = {
    "MemoryMachineGraph": machine_graph,
    "MemoryMachinePartitionNKeysMap": n_keys_map,
    "PlanNTimeSteps": 100,
    "JsonFolder": temp,
    "IPAddress": "192.168.240.253",
    "ResetMachineOnStartupFlag": False,
    "BMPDetails": None,
    "DownedChipsDetails": set(),
    "DownedCoresDetails": set(),
    "DownedLinksDetails": set(),
    "BoardVersion": 3,
    "AutoDetectBMPFlag": False,
    "ScampConnectionData": None,
    "BootPortNum": None,
    "MaxSDRAMSize": None,
    "RepairMachine": False,
    "IgnoreBadEthernets": False,
    "ReportFolder": temp
}

algorithms = ["MachineGenerator", "SpreaderPlacer", "WriteJsonMachineGraph","MallocBasedChipIDAllocator"]
executor = PACMANAlgorithmExecutor(
    algorithms, [], inputs, [], [], [],
    xml_paths=get_front_end_common_pacman_xml_paths())
executor.execute_mapping()
