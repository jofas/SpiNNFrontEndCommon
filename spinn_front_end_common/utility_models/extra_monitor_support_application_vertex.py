from pacman.executor.injection_decorator import inject_items
from pacman.model.graphs.application import ApplicationVertex
from spinn_front_end_common.abstract_models import \
    AbstractHasAssociatedBinary, AbstractGeneratesDataSpecification, \
    AbstractProvidesIncomingPartitionConstraints
from spinn_front_end_common.utilities import constants
from spinn_front_end_common.utility_models.\
    extra_monitor_support_machine_vertex import \
    ExtraMonitorSupportMachineVertex
from spinn_utilities.overrides import overrides


class ExtraMonitorSupportApplicationVertex(
        ApplicationVertex, AbstractHasAssociatedBinary,
        AbstractGeneratesDataSpecification,
        AbstractProvidesIncomingPartitionConstraints):

    __slots__ = (
        # place holder for a connection if needed
        "_connection"
    )

    def __init__(self, constraints, is_ethernet_connected=False,
                 connection=None):
        ApplicationVertex.__init__(
            self, label="ExtraMonitorSupportApplicationVertex",
            constraints=constraints)
        AbstractHasAssociatedBinary.__init__(self)
        AbstractProvidesIncomingPartitionConstraints.__init__(self)
        AbstractGeneratesDataSpecification.__init__(self)
        self._connection = \
            ExtraMonitorSupportMachineVertex.generate_connection(
                is_ethernet_connected, connection)

    @overrides(ApplicationVertex.create_machine_vertex)
    def create_machine_vertex(self, vertex_slice, resources_required,
                              label=None, constraints=None):
        return ExtraMonitorSupportMachineVertex(
            constraints=constraints, connection=self._connection)

    @property
    @overrides(ApplicationVertex.n_atoms)
    def n_atoms(self):
        return 1

    @overrides(AbstractHasAssociatedBinary.get_binary_start_type)
    def get_binary_start_type(self):
        return ExtraMonitorSupportMachineVertex.static_get_binary_start_type()

    @overrides(AbstractHasAssociatedBinary.get_binary_file_name)
    def get_binary_file_name(self):
        return ExtraMonitorSupportMachineVertex.static_get_binary_file_name()

    @overrides(ApplicationVertex.get_resources_used_by_atoms)
    def get_resources_used_by_atoms(self, vertex_slice):
        return ExtraMonitorSupportMachineVertex.static_resources_required(
            self._connection)

    @inject_items({"routing_info": "MemoryRoutingInfos",
                   "machine_graph": "MemoryMachineGraph",
                   "tags": "MemoryTags"})
    @overrides(AbstractGeneratesDataSpecification.generate_data_specification,
               additional_arguments={"routing_info", "machine_graph", "tags"})
    def generate_data_specification(
            self, spec, placement, routing_info, machine_graph, tags):
        placement.vertex.generate_data_specification(
            spec, placement, routing_info, machine_graph, tags)

    @inject_items({"application_graph": "MemoryApplicationGraph"})
    @overrides(AbstractProvidesIncomingPartitionConstraints.
               get_incoming_partition_constraints,
               additional_arguments={"application_graph"})
    def get_incoming_partition_constraints(self, partition, application_graph):
        if partition.identifier != \
                constants.PARTITION_ID_FOR_MULTICAST_DATA_SPEED_UP:
            raise Exception("do not recognise this partition identifier")

        vertex_partition = list()
        incoming_edges = application_graph.get_edges_ending_at_vertex(self)
        for incoming_edge in incoming_edges:
            partition = application_graph.\
                get_outgoing_edge_partition_starting_at_vertex(
                    incoming_edge.pre_vertex,
                    constants.PARTITION_ID_FOR_MULTICAST_DATA_SPEED_UP)
            vertex_partition.append(partition)
        return ExtraMonitorSupportMachineVertex.\
            static_get_incoming_partition_constraints(
                partition, vertex_partition)
