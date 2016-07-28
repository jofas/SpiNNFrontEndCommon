# pacman imports
from pacman.executor.injection_decorator import requires_injection, inject
from pacman.model.abstract_classes.impl.constrained_object import \
    ConstrainedObject
from pacman.model.decorators.delegates_to import delegates_to
from pacman.model.decorators.overrides import overrides
from pacman.model.graphs.application.abstract_application_vertex \
    import AbstractApplicationVertex

# spinn front end imports
from pacman.model.resources.cpu_cycles_per_tick_resource import \
    CPUCyclesPerTickResource
from pacman.model.resources.dtcm_resource import DTCMResource
from pacman.model.resources.resource_container import ResourceContainer
from pacman.model.resources.sdram_resource import SDRAMResource
from spinn_front_end_common.abstract_models.impl.data_specable_vertex import \
    DataSpecableVertex
from spinn_front_end_common.utilities.exceptions import ConfigurationException
from spinn_front_end_common.utility_models\
    .live_packet_gather_machine_vertex \
    import LivePacketGatherMachineVertex

# spinnman imports
from spinnman.messages.eieio.eieio_type import EIEIOType
from spinnman.messages.eieio.eieio_prefix import EIEIOPrefix


class LivePacketGather(
        DataSpecableVertex, AbstractApplicationVertex):
    """ A model which stores all the events it receives during a timer tick\
        and then compresses them into Ethernet packets and sends them out of\
        a spinnaker machine.
    """

    def __init__(self, machine_time_step, timescale_factor, ip_address,
                 port, board_address=None, tag=None, strip_sdp=True,
                 use_prefix=False, key_prefix=None, prefix_type=None,
                 message_type=EIEIOType.KEY_32_BIT, right_shift=0,
                 payload_as_time_stamps=True, use_payload_prefix=True,
                 payload_prefix=None, payload_right_shift=0,
                 number_of_packets_sent_per_time_step=0, constraints=None,
                 label=None):
        """
        """
        if ((message_type == EIEIOType.KEY_PAYLOAD_32_BIT or
             message_type == EIEIOType.KEY_PAYLOAD_16_BIT) and
                use_payload_prefix and payload_as_time_stamps):
            raise ConfigurationException(
                "Timestamp can either be included as payload prefix or as "
                "payload to each key, not both")
        if ((message_type == EIEIOType.KEY_32_BIT or
             message_type == EIEIOType.KEY_16_BIT) and
                not use_payload_prefix and payload_as_time_stamps):
            raise ConfigurationException(
                "Timestamp can either be included as payload prefix or as"
                " payload to each key, but current configuration does not "
                "specify either of these")
        if (not isinstance(prefix_type, EIEIOPrefix) and
                prefix_type is not None):
            raise ConfigurationException(
                "the type of a prefix type should be of a EIEIOPrefix, "
                "which can be located in :"
                "SpinnMan.messages.eieio.eieio_prefix_type")
        if label is None:
            self._label = "Live Packet Gatherer"
        else:
            self._label = label

        DataSpecableVertex.__init__(self)
        AbstractApplicationVertex.__init__(self)

        self._constraints = ConstrainedObject(constraints)
        self._machine_time_step = machine_time_step
        self._time_scale_factor = timescale_factor
        self._label = label

        # storage objects
        self._iptags = None

        # add constraints the vertex decides it needs
        constraints_to_add = \
            LivePacketGatherMachineVertex.get_constraints(
                ip_address, port, strip_sdp, board_address, tag)
        for constraint in constraints_to_add:
            self.add_constraint(constraint)

        self._prefix_type = prefix_type
        self._use_prefix = use_prefix
        self._key_prefix = key_prefix
        self._message_type = message_type
        self._right_shift = right_shift
        self._payload_as_time_stamps = payload_as_time_stamps
        self._use_payload_prefix = use_payload_prefix
        self._payload_prefix = payload_prefix
        self._payload_right_shift = payload_right_shift
        self._number_of_packets_sent_per_time_step = \
            number_of_packets_sent_per_time_step

    @overrides(AbstractApplicationVertex.create_machine_vertex)
    def create_machine_vertex(
            self, vertex_slice, resources_required, constraints=None):
        return LivePacketGatherMachineVertex(
            self._label, self._machine_time_step, self._timescale_factor,
            self._use_prefix, self._key_prefix, self._prefix_type,
            self._message_type, self._right_shift,
            self._payload_as_time_stamps, self._use_payload_prefix,
            self._payload_prefix, self._payload_right_shift,
            self._number_of_packets_sent_per_time_step,
            constraints=constraints)

    @property
    @overrides(AbstractApplicationVertex.model_name)
    def model_name(self):
        """ Human readable form of the model name
        """
        return "live packet gather"

    @overrides(DataSpecableVertex.get_binary_file_name)
    def get_binary_file_name(self):
        return 'live_packet_gather.aplx'

    @property
    @overrides(AbstractApplicationVertex.label)
    def label(self):
        return self._label

    @delegates_to("_constraints", ConstrainedObject.add_constraints)
    def add_constraints(self, constraints):
        pass

    @property
    @delegates_to("_constraints", ConstrainedObject.constraints)
    def constraints(self):
        pass

    @delegates_to("_constraints", ConstrainedObject.add_constraint)
    def add_constraint(self, constraint):
        pass

    @property
    @overrides(AbstractApplicationVertex.n_atoms)
    def n_atoms(self):
        return 1

    @overrides(AbstractApplicationVertex.get_resources_used_by_atoms)
    def get_resources_used_by_atoms(self, vertex_slice):
        return ResourceContainer(
            sdram=SDRAMResource(
                LivePacketGatherMachineVertex.get_sdram_usage()),
            dtcm=DTCMResource(LivePacketGatherMachineVertex.get_dtcm_usage()),
            cpu_cycles=CPUCyclesPerTickResource(
                LivePacketGatherMachineVertex.get_cpu_usage()))

    @overrides(DataSpecableVertex.generate_data_specification)
    @requires_injection(["MemoryIptags"])
    def generate_data_specification(self, spec, placement):

        # needs to set it directly, as the machine vertex also impliemnts this
        # interface, incase its being used in a machine graph without a
        # application graph
        placement.vertex.set_iptags(self._iptags)

        # generate spec for the machine vertex
        placement.vertex.generate_data_spec(spec, placement)

    @inject("MemoryIptags")
    def set_iptags(self, iptags):
        self._iptags = iptags
