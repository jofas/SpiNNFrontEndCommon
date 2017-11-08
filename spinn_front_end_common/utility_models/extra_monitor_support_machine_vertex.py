import struct

import math

import time
from enum import Enum

from pacman.executor.injection_decorator import inject_items
from pacman.model.graphs.common import EdgeTrafficType
from pacman.model.graphs.machine import MachineVertex
from pacman.model.resources import ResourceContainer, SDRAMResource, \
    IPtagResource

from spinn_front_end_common.abstract_models import \
    AbstractHasAssociatedBinary, AbstractGeneratesDataSpecification, \
    AbstractProvidesIncomingPartitionConstraints
from spinn_front_end_common.utilities.utility_objs import ExecutableType
from spinn_front_end_common.utilities.utility_objs.\
    extra_monitor_scp_processes.read_status_process import \
    ReadStatusProcess
from spinn_front_end_common.utilities.utility_objs.\
    extra_monitor_scp_processes.reset_counters_process import \
    ResetCountersProcess
from spinn_front_end_common.utilities.utility_objs.\
    extra_monitor_scp_processes.set_packet_types_process import \
    SetPacketTypesProcess
from spinn_front_end_common.utilities.utility_objs.\
    extra_monitor_scp_processes.set_router_emergency_timeout_process import \
    SetRouterEmergencyTimeoutProcess
from spinn_front_end_common.utilities.utility_objs.\
    extra_monitor_scp_processes.set_router_timeout_process import \
    SetRouterTimeoutProcess
from spinn_front_end_common.utilities import constants

from spinn_machine import CoreSubsets

from spinn_utilities.overrides import overrides

from spinnman.connections.udp_packet_connections import UDPConnection
from spinnman.exceptions import SpinnmanTimeoutException
from spinnman.messages.sdp import SDPMessage, SDPFlag, SDPHeader


class ExtraMonitorSupportMachineVertex(
        MachineVertex, AbstractHasAssociatedBinary,
        AbstractGeneratesDataSpecification,
        AbstractProvidesIncomingPartitionConstraints):

    __slots__ = [

        # if we reinject mc packets
        "_reinject_multicast",

        # if we reinject point to point packets
        "_reinject_point_to_point",

        # if we reinject nearest neighbour packets
        "_reinject_nearest_neighbour",

        # if we reinject fixed route packets
        "_reinject_fixed_route",

        # data reception functionality
        "_view",

        # the biggest seq num expected at this time
        "_max_seq_num",

        # the output store for data extraction
        "_output",

        # the list of lost seq nums
        "_lost_seq_nums",

        # data connection if used
        "_connection"
    ]

    _EXTRA_MONITOR_DSG_REGIONS = Enum(
        value="_EXTRA_MONITOR_DSG_REGIONS",
        names=[('CONFIG', 0),
               ('DATA_SPEED_CONFIG', 1),
               ('DATA_RECEPTION_CONFIG', 2)])

    # the identifier used by the iptag for this communication
    _DATA_TRAFFIC_IDENTIFIER = "speed_up_data_extraction_channel"

    # what type of traffic is being used via its edges
    # TRAFFIC_TYPE = EdgeTrafficType.MULTICAST
    TRAFFIC_TYPE = EdgeTrafficType.FIXED_ROUTE

    _CONFIG_REGION_REINEJCTOR_SIZE_IN_BYTES = 4 * 4
    _CONFIG_DATA_SPEED_UP_SIZE_IN_BYTES = 1 * 4
    _CONFIG_DATA_RECEPTION_SIZE_IN_BYTES = 2 * 4

    # size of config region in bytes
    CONFIG_SIZE = 8

    # items of data a SDP packet can hold when scp header removed
    DATA_PER_FULL_PACKET = 68  # 272 bytes as removed scp header

    # size of items the sequence number uses
    SEQUENCE_NUMBER_SIZE_IN_ITEMS = 1

    # the size of the sequence number in bytes
    SEQUENCE_NUMBER_SIZE = 4

    # items of data from sdp pakcet with a seqeunce number
    DATA_PER_FULL_PACKET_WITH_SEQUENCE_NUM = \
        DATA_PER_FULL_PACKET - SEQUENCE_NUMBER_SIZE_IN_ITEMS

    # converter between words and bytes
    WORD_TO_BYTE_CONVERTER = 4

    # time outs used by the protocol for seperate bits
    TIMEOUT_PER_RECEIVE_IN_SECONDS = 1
    TIME_OUT_FOR_SENDING_IN_SECONDS = 0.01

    # command ids for the sdp packets
    SDP_PACKET_START_SENDING_COMMAND_ID = 100
    SDP_PACKET_START_MISSING_SEQ_COMMAND_ID = 1000
    SDP_PACKET_MISSING_SEQ_COMMAND_ID = 1001

    # number of items used up by the re transmit code for its header
    SDP_RETRANSMISSION_HEADER_SIZE = 2

    # what governs a end flag
    END_FLAG = 0xFFFFFFFF

    # base key (really nasty hack)
    BASE_KEY = 0xFFFFFFF9
    BASE_MASK = 0xFFFFFFFB

    # the size in bytes of the end flag
    END_FLAG_SIZE = 4

    # the amount of bytes the n bytes takes up
    N_PACKETS_SIZE = 4

    # the amount of bytes the data length will take up
    LENGTH_OF_DATA_SIZE = 4

    _EXTRA_MONITOR_COMMANDS = Enum(
        value="EXTRA_MONITOR_COMMANDS",
        names=[("SET_ROUTER_TIMEOUT", 0),
               ("SET_ROUTER_EMERGENCY_TIMEOUT", 1),
               ("SET_PACKET_TYPES", 2),
               ("GET_STATUS", 3),
               ("RESET_COUNTERS", 4),
               ("EXIT", 5)])

    def __init__(
            self, constraints, reinject_multicast=True,
            reinject_point_to_point=False, reinject_nearest_neighbour=False,
            reinject_fixed_route=False, is_ethernet_connected=False,
            connection=None):
        """ constructor
        
        :param constraints: constraints on this vertex
        :param reinject_multicast: if we reinject mc packets
        :param reinject_point_to_point: if we reinject point to point packets
        :param reinject_nearest_neighbour: if we reinject nearest neighbour \
        packets
        :param reinject_fixed_route: if we reinject fixed route packets
        :param is_ethernet_connected: bool stating if this vertex is located\
            on a ethernet connected chip.
        """
        MachineVertex.__init__(
            self, label="ExtraMonitorSupportMachineVertex",
            constraints=constraints)
        AbstractHasAssociatedBinary.__init__(self)
        AbstractGeneratesDataSpecification.__init__(self)
        AbstractProvidesIncomingPartitionConstraints.__init__(self)

        # reinjection functionality
        self._reinject_multicast = reinject_multicast
        self._reinject_point_to_point = reinject_point_to_point
        self._reinject_nearest_neighbour = reinject_nearest_neighbour
        self._reinject_fixed_route = reinject_fixed_route

        # data reception functionality
        self._view = None
        self._max_seq_num = None
        self._output = None
        self._lost_seq_nums = list()

        # rectify connection
        self._connection = self.generate_connection(
            is_ethernet_connected, connection)

    @staticmethod
    def generate_connection(is_ethernet_connected, connection):
        if connection is None:
            # create socket if required
            if is_ethernet_connected:
                return UDPConnection(local_host=None)
            else:
                return None
        else:
            return connection

    @property
    def reinject_multicast(self):
        return self._reinject_multicast

    @property
    def reinject_point_to_point(self):
        return self._reinject_point_to_point

    @property
    def reinject_nearest_neighbour(self):
        return self._reinject_nearest_neighbour

    @property
    def reinject_fixed_route(self):
        return self._reinject_fixed_route

    @property
    @overrides(MachineVertex.resources_required)
    def resources_required(self):
        return self.static_resources_required(self._connection)

    @staticmethod
    def static_resources_required(connection):
        iptags = list()
        if connection is not None:
            iptags.append(IPtagResource(
                port=connection.local_port, strip_sdp=True,
                ip_address="localhost",
                traffic_identifier=
                ExtraMonitorSupportMachineVertex.
                _DATA_TRAFFIC_IDENTIFIER))
        return ResourceContainer(
            sdram=SDRAMResource(
                sdram=ExtraMonitorSupportMachineVertex.
                _CONFIG_REGION_REINEJCTOR_SIZE_IN_BYTES +
                ExtraMonitorSupportMachineVertex.
                _CONFIG_DATA_SPEED_UP_SIZE_IN_BYTES +
                ExtraMonitorSupportMachineVertex.
                _CONFIG_DATA_RECEPTION_SIZE_IN_BYTES),
            iptags=iptags)

    @overrides(AbstractHasAssociatedBinary.get_binary_start_type)
    def get_binary_start_type(self):
        return self.static_get_binary_start_type()

    @staticmethod
    def static_get_binary_start_type():
        return ExecutableType.RUNNING

    @overrides(AbstractHasAssociatedBinary.get_binary_file_name)
    def get_binary_file_name(self):
        return self.static_get_binary_file_name()

    @staticmethod
    def static_get_binary_file_name():
        return "extra_monitor_support.aplx"

    @inject_items({"machine_graph": "MemoryMachineGraph"})
    @overrides(AbstractProvidesIncomingPartitionConstraints.
               get_incoming_partition_constraints,
               additional_arguments={"machine_graph"})
    def get_incoming_partition_constraints(self, partition, machine_graph):
        if partition.identifier != \
                constants.PARTITION_ID_FOR_MULTICAST_DATA_SPEED_UP:
            raise Exception("do not recognise this partition identifier")

        vertex_partition = list()
        for incoming_edge in machine_graph.get_edges_ending_at_vertex(self):
            partition = \
                machine_graph.get_outgoing_edge_partition_starting_at_vertex(
                    incoming_edge.pre_vertex,
                    constants.PARTITION_ID_FOR_MULTICAST_DATA_SPEED_UP)
            vertex_partition.append(partition)
        return self.static_get_incoming_partition_constraints(
            partition, vertex_partition)

    @staticmethod
    def static_get_incoming_partition_constraints(partition, vertex_partition):
        constraints = list()
        # if partition.traffic_type == EdgeTrafficType.MULTICAST:
        #    constraints.append(
        #        FixedKeyAndMaskConstraint([
        #            BaseKeyAndMask(
        #                ExtraMonitorSupportMachineVertex.BASE_KEY,
        #                ExtraMonitorSupportMachineVertex.BASE_MASK
        #            )]))
        #    constraints.append(ShareKeyConstraint(vertex_partition))
        return constraints

    @inject_items({"routing_info": "MemoryRoutingInfos",
                   "machine_graph": "MemoryMachineGraph",
                   "tags": "MemoryTags"})
    @overrides(AbstractGeneratesDataSpecification.generate_data_specification,
               additional_arguments={"routing_info", "machine_graph", "tags"})
    def generate_data_specification(
            self, spec, placement, routing_info, machine_graph, tags):
        self._generate_reinjector_functionality_data_specification(spec)
        self._generate_data_speed_up_functionality_data_specification(
            spec, routing_info, machine_graph)
        self._generate_data_reception_functionality_data_specification(
            spec, iptags=tags.get_ip_tags_for_vertex(self))
        spec.end_specification()

    def _generate_data_reception_functionality_data_specification(
            self, spec, iptags):
        spec.reserve_memory_region(
            region=self._EXTRA_MONITOR_DSG_REGIONS.DATA_RECEPTION_CONFIG.value,
            size=self._CONFIG_DATA_RECEPTION_SIZE_IN_BYTES,
            label="data_reception functionality config region")
        spec.switch_write_focus(
            self._EXTRA_MONITOR_DSG_REGIONS.DATA_RECEPTION_CONFIG.value)

        # verify that the correct number of iptags are placed on this vertex
        if iptags is not None and len(iptags) != 1:
            raise Exception(
                "should only have 1 or 0 iptags associated with this vertex")

        if self._connection is not None:
            spec.write_value(1)
        else:
            spec.write_value(0)

        if self._connection is not None:
            tag = iptags[0]
            if (tag.traffic_identifier !=
                    ExtraMonitorSupportMachineVertex._DATA_TRAFFIC_IDENTIFIER):
                raise Exception(
                    "The only tag here should be of traffic type {}".format(
                        ExtraMonitorSupportMachineVertex.
                        _DATA_TRAFFIC_IDENTIFIER))
            spec.write_value(tag.tag)

    def _generate_data_speed_up_functionality_data_specification(
            self, spec, routing_info, machine_graph):
        spec.reserve_memory_region(
            region=self._EXTRA_MONITOR_DSG_REGIONS.DATA_SPEED_CONFIG.value,
            size=self._CONFIG_DATA_SPEED_UP_SIZE_IN_BYTES,
            label="data_speed functionality config region")
        spec.switch_write_focus(
            self._EXTRA_MONITOR_DSG_REGIONS.DATA_SPEED_CONFIG.value)

        if self.TRAFFIC_TYPE == EdgeTrafficType.MULTICAST:
            base_key = routing_info.get_first_key_for_edge(
                list(machine_graph.get_edges_starting_at_vertex(self))[0])
            spec.write_value(base_key)
        else:
            spec.write_value(self.BASE_KEY)

    def _generate_reinjector_functionality_data_specification(self, spec):
        spec.reserve_memory_region(
            region=self._EXTRA_MONITOR_DSG_REGIONS.CONFIG.value,
            size=self._CONFIG_REGION_REINEJCTOR_SIZE_IN_BYTES,
            label="re-injection functionality config region")

        spec.switch_write_focus(self._EXTRA_MONITOR_DSG_REGIONS.CONFIG.value)
        for value in [
                self._reinject_multicast, self._reinject_point_to_point,
                self._reinject_fixed_route,
                self._reinject_nearest_neighbour]:
            if value:
                spec.write_value(0)
            else:
                spec.write_value(1)

    def set_router_time_outs(
            self, timeout_mantissa, timeout_exponent, transceiver, placements,
            extra_monitor_cores_to_set):
        """ supports setting of the router time outs for a set of chips via
         their extra monitor cores.
        
        :param timeout_mantissa: what timeout mantissa to set it to
        :type timeout_exponent: int
        :type timeout_mantissa: int
        :param timeout_exponent: what timeout exponent to set it to
        :param transceiver: the spinnman interface
        :param placements: placements object
        :param extra_monitor_cores_to_set: which vertices to use
        :rtype: None 
        """

        core_subsets = self._convert_vertices_to_core_subset(
            extra_monitor_cores_to_set, placements)
        process = SetRouterTimeoutProcess(
            transceiver.scamp_connection_selector)
        process.set_timeout(
            timeout_mantissa, timeout_exponent, core_subsets,
            self._EXTRA_MONITOR_COMMANDS.SET_ROUTER_TIMEOUT)

    def set_reinjection_router_emergency_timeout(
            self, timeout_mantissa, timeout_exponent, transceiver, placements,
            extra_monitor_cores_to_set):
        """ Sets the timeout of the routers

        :param timeout_mantissa: The mantissa of the timeout value, between 0\
                and 15
        :type timeout_mantissa: int
        :param timeout_exponent: The exponent of the timeout value, between 0\
                and 15
        :type timeout_exponent: int
        :param transceiver: the spinnMan instance
        :param placements: the placements object
        :param extra_monitor_cores_to_set: the set of vertices to 
        change the local chip for.
        """
        core_subsets = self._convert_vertices_to_core_subset(
            extra_monitor_cores_to_set, placements)
        process = SetRouterEmergencyTimeoutProcess(
            transceiver.scamp_connection_selector)
        process.set_timeout(
            timeout_mantissa, timeout_exponent, core_subsets,
            self._EXTRA_MONITOR_COMMANDS.SET_ROUTER_EMERGENCY_TIMEOUT)

    def reset_reinjection_counters(
            self, transceiver, placements, extra_monitor_cores_to_set):
        """ Resets the counters for re injection
        """
        core_subsets = self._convert_vertices_to_core_subset(
            extra_monitor_cores_to_set, placements)
        process = ResetCountersProcess(transceiver.scamp_connection_selector)
        process.reset_counters(
            core_subsets, self._EXTRA_MONITOR_COMMANDS.RESET_COUNTERS)

    def get_reinjection_status(self, placements, transceiver):
        """ gets the reinjection status from this extra monitor vertex
        
        :param transceiver: the spinnMan interface
        :param placements: the placements object
        :return: the reinjection status for this vertex
        """
        placement = placements.get_placement_of_vertex(self)
        process = ReadStatusProcess(transceiver.scamp_connection_selector)
        return process.get_reinjection_status(
            placement.x, placement.y, placement.p,
            self._EXTRA_MONITOR_COMMANDS.GET_STATUS)

    def get_reinjection_status_for_vertices(
            self, placements, extra_monitor_cores_for_data, transceiver):
        """ gets the reinjection status from a set of extra monitor cores
        
        :param placements: the placements object
        :param extra_monitor_cores_for_data: the extra monitor cores to get\
         status from
        :param transceiver: the spinnMan interface
        :rtype: None 
        """
        core_subsets = self._convert_vertices_to_core_subset(
            extra_monitor_cores_for_data, placements)
        process = ReadStatusProcess(transceiver.scamp_connection_selector)
        return process.get_reinjection_status_for_core_subsets(
            core_subsets, self._EXTRA_MONITOR_COMMANDS.GET_STATUS)

    def set_reinjection_packets(
            self, placements, transceiver, point_to_point=None, multicast=None,
            nearest_neighbour=None, fixed_route=None):
        """
        
        :param placements: placements object
        :param transceiver: spinnman instance
        :param point_to_point: bool stating if point to point should be set,\
         or None if left as before
        :param multicast: bool stating if multicast should be set,\
         or None if left as before
        :param nearest_neighbour: bool stating if nearest neighbour should be \
        set, or None if left as before
        :param fixed_route: bool stating if fixed route should be set, or \
        None if left as before.
        :rtype: None 
        """
        if multicast is not None:
            self._reinject_multicast = multicast
        if point_to_point is not None:
            self._reinject_point_to_point = point_to_point
        if nearest_neighbour is not None:
            self._reinject_nearest_neighbour = nearest_neighbour
        if fixed_route is not None:
            self._reinject_fixed_route = fixed_route

        placement = placements.get_placement_of_vertex(self)
        core_subsets = CoreSubsets()
        core_subsets.add_processor(placement.x, placement.y, placement.p)
        process = SetPacketTypesProcess(transceiver.scamp_connection_selector)
        process.set_packet_types(
            core_subsets, self._reinject_point_to_point,
            self._reinject_multicast, self._reinject_nearest_neighbour,
            self._reinject_fixed_route,
            self._EXTRA_MONITOR_COMMANDS.GET_STATUS)

    @staticmethod
    def _convert_vertices_to_core_subset(
            extra_monitor_cores_to_set, placements):
        """ converts vertices into core subsets. 
        
        :param extra_monitor_cores_to_set: the vertices to convert to core \
        subsets
        :param placements: the placements object
        :return: the converts CoreSubSets to the vertices
        """
        core_subsets = CoreSubsets()
        for vertex in extra_monitor_cores_to_set:
            if not isinstance(vertex, ExtraMonitorSupportMachineVertex):
                raise Exception(
                    "can only use ExtraMonitorSupportMachineVertex to set "
                    "the router time out")
            placement = placements.get_placement_of_vertex(vertex)
            core_subsets.add_processor(placement.x, placement.y, placement.p)
        return core_subsets

    def get_data(
            self, transceiver, placement, memory_address, length_in_bytes,
            extra_monitor_cores_for_router_timeout, placements):
        """ gets data from a given core and memory address. 

        :param transceiver: spinnman instance
        :param placement: placement object for where to get data from
        :param memory_address: the address in sdram to start reading from
        :param length_in_bytes: the length of data to read in bytes
        :param extra_monitor_cores_for_router_timeout: cores 
        to set router timeout for
        :param placements: the placements object
        :return: byte array of the data
        """
        # set time out
        extra_monitor_cores_for_router_timeout[0].set_router_time_outs(
            15, 15, transceiver, placements,
            extra_monitor_cores_for_router_timeout)

        data = struct.pack(
            "<III", *[self.SDP_PACKET_START_SENDING_COMMAND_ID,
                      memory_address, length_in_bytes])

        # print "sending to core {}:{}:{}".format(
        #    placement.x, placement.y, placement.p)
        message = SDPMessage(
            sdp_header=SDPHeader(
                destination_chip_x=placement.x,
                destination_chip_y=placement.y,
                destination_cpu=placement.p,
                destination_port=
                constants.SDP_PORTS.EXTRA_MONITOR_CORE_DATA_SPEED_UP.value,
                flags=SDPFlag.REPLY_NOT_EXPECTED),
            data=data)

        # send
        transceiver.send_sdp_message(message=message)

        # receive
        finished = False
        first = True
        seq_num = 1
        seq_nums = set()
        while not finished:
            try:
                data = self._connection.receive(
                    timeout=self.TIMEOUT_PER_RECEIVE_IN_SECONDS)

                first, seq_num, seq_nums, finished = \
                    self._process_data(
                        data, first, seq_num, seq_nums, finished,
                        placement,
                        transceiver)
            except SpinnmanTimeoutException:
                if not finished:
                    finished = self._determine_and_retransmit_missing_seq_nums(
                        seq_nums, transceiver, placement)

        # self._check(seq_nums)
        extra_monitor_cores_for_router_timeout[0].set_router_time_outs(
            15, 4, transceiver, placements,
            extra_monitor_cores_for_router_timeout)

        return self._output, self._lost_seq_nums

    def _calculate_missing_seq_nums(self, seq_nums):
        """ determines which seq numbers we've missed

        :param seq_nums: the set already acquired
        :return: list of missing seq nums
        """
        if self._max_seq_num is None:
            raise Exception(
                "Have not heard from the machine. Something went boom!")

        missing_seq_nums = list()
        for seq_num in range(1, self._max_seq_num):
            if seq_num not in seq_nums:
                missing_seq_nums.append(seq_num)
        return missing_seq_nums

    def _determine_and_retransmit_missing_seq_nums(
            self, seq_nums, transceiver, placement):
        """ determines if there are any missing seq nums, and if so 
        retransmits the missing seq nums back to the core for retransmission

        :param seq_nums: the seq nums already received
        :param transceiver: spinnman instance
        :param placement: placement instance
        :return: true or false based on if finished or not
        """
        # locate missing seq nums from pile
        missing_seq_nums = self._calculate_missing_seq_nums(seq_nums)
        self._lost_seq_nums.append(len(missing_seq_nums))
        # self._print_missing(seq_nums)
        if len(missing_seq_nums) == 0:
            return True

        # print "doing retransmission"
        # figure n packets given the 2 formats
        n_packets = 1
        length_via_format2 = \
            len(missing_seq_nums) - (self.DATA_PER_FULL_PACKET - 2)
        if length_via_format2 > 0:
            n_packets += int(math.ceil(
                float(length_via_format2) /
                float(self.DATA_PER_FULL_PACKET - 1)))

        # transmit missing seq as a new sdp packet
        first = True
        seq_num_offset = 0
        for packet_count in range(0, n_packets):
            length_left_in_packet = self.DATA_PER_FULL_PACKET
            offset = 0
            data = None
            size_of_data_left_to_transmit = None

            # if first, add n packets to list
            if first:

                # get left over space / data size
                size_of_data_left_to_transmit = min(
                    length_left_in_packet - 2,
                    len(missing_seq_nums) - seq_num_offset)

                # build data holder accordingly
                data = bytearray(
                    (size_of_data_left_to_transmit + 2) *
                    self.WORD_TO_BYTE_CONVERTER)

                # pack flag and n packets
                struct.pack_into(
                    "<I", data, offset,
                    self.SDP_PACKET_START_MISSING_SEQ_COMMAND_ID)
                struct.pack_into(
                    "<I", data, self.WORD_TO_BYTE_CONVERTER, n_packets)

                # update state
                offset += 2 * self.WORD_TO_BYTE_CONVERTER
                length_left_in_packet -= 2
                first = False

            else:  # just add data
                # get left over space / data size
                size_of_data_left_to_transmit = min(
                    self.DATA_PER_FULL_PACKET_WITH_SEQUENCE_NUM,
                    len(missing_seq_nums) - seq_num_offset)

                # build data holder accordingly
                data = bytearray(
                    (size_of_data_left_to_transmit + 1) *
                    self.WORD_TO_BYTE_CONVERTER)

                # pack flag
                struct.pack_into(
                    "<I", data, offset,
                    self.SDP_PACKET_MISSING_SEQ_COMMAND_ID)
                offset += 1 * self.WORD_TO_BYTE_CONVERTER
                length_left_in_packet -= 1

            # fill data field
            struct.pack_into(
                "<{}I".format(size_of_data_left_to_transmit), data, offset,
                *missing_seq_nums[
                 seq_num_offset:
                 seq_num_offset + size_of_data_left_to_transmit])
            seq_num_offset += length_left_in_packet

            # build sdp message
            message = SDPMessage(
                sdp_header=SDPHeader(
                    destination_chip_x=placement.x,
                    destination_chip_y=placement.y,
                    destination_cpu=placement.p,
                    destination_port=
                    constants.SDP_PORTS.EXTRA_MONITOR_CORE_DATA_SPEED_UP.value,
                    flags=SDPFlag.REPLY_NOT_EXPECTED),
                data=str(data))

            # debug
            # self._print_out_packet_data(data)

            # send message to core
            transceiver.send_sdp_message(message=message)

            # sleep for ensuring core doesnt lose packets
            time.sleep(self.TIME_OUT_FOR_SENDING_IN_SECONDS)
            # self._print_packet_num_being_sent(packet_count, n_packets)
        return False

    def _process_data(self, data, first, seq_num, seq_nums, finished,
                      placement, transceiver):
        """ Takes a packet and processes it see if we're finished yet

        :param data: the packet data
        :param first: if the packet is the first packet, in which has extra 
        data in header
        :param seq_num: the seq number of the packet 
        :param seq_nums: the list of seq nums received so far
        :param finished: bool which states if finished or not 
        :param placement: placement object for location on machine
        :param transceiver: spinnman instance
        :return: set of data items, if its the first packet, the list of seq
        nums, the seq num received and if its finished
        """
        # self._print_out_packet_data(data)
        length_of_data = len(data)
        if first:
            length = struct.unpack_from("<I", data, 0)[0]
            first = False
            self._output = bytearray(length)
            self._view = memoryview(self._output)
            self._write_into_view(
                0, length_of_data - self.LENGTH_OF_DATA_SIZE,
                data,
                self.LENGTH_OF_DATA_SIZE, length_of_data, seq_num,
                length_of_data, False)

            # deduce max seq num for future use
            self._max_seq_num = self.calculate_max_seq_num()

        else:  # some data packet
            first_packet_element = struct.unpack_from(
                "<I", data, 0)[0]
            last_mc_packet = struct.unpack_from(
                "<I", data, length_of_data - self.END_FLAG_SIZE)[0]

            # if received a last flag on its own, its during retransmission.
            #  check and try again if required
            if (last_mc_packet == self.END_FLAG and
                    length_of_data == self.END_FLAG_SIZE):
                if not self._check(seq_nums):
                    finished = self._determine_and_retransmit_missing_seq_nums(
                        placement=placement, transceiver=transceiver,
                        seq_nums=seq_nums)
            else:
                # this flag can be dropped at some point
                seq_num = first_packet_element
                # print "seq num = {}".format(seq_num)
                if seq_num > self._max_seq_num:
                    raise Exception(
                        "got an insane sequence number. got {} when "
                        "the max is {} with a length of {}".format(
                            seq_num, self._max_seq_num, length_of_data))
                seq_nums.add(seq_num)

                # figure offset for where data is to be put
                offset = self._calculate_offset(seq_num)

                # write excess data as required
                if last_mc_packet == self.END_FLAG:

                    # adjust for end flag
                    true_data_length = (
                        length_of_data - self.END_FLAG_SIZE -
                        self.SEQUENCE_NUMBER_SIZE)

                    # write data
                    self._write_into_view(
                        offset, offset + true_data_length, data,
                        self.SEQUENCE_NUMBER_SIZE,
                        length_of_data - self.END_FLAG_SIZE, seq_num,
                        length_of_data, True)

                    # check if need to retry
                    if not self._check(seq_nums):
                        finished = \
                            self._determine_and_retransmit_missing_seq_nums(
                                placement=placement, transceiver=transceiver,
                                seq_nums=seq_nums)
                    else:
                        finished = True

                else:  # full block of data, just write it in
                    true_data_length = (
                        offset + length_of_data - self.SEQUENCE_NUMBER_SIZE)
                    self._write_into_view(
                        offset, true_data_length, data,
                        self.SEQUENCE_NUMBER_SIZE,
                        length_of_data, seq_num, length_of_data, False)
        return first, seq_num, seq_nums, finished

    def _calculate_offset(self, seq_num):
        offset = (seq_num * self.DATA_PER_FULL_PACKET_WITH_SEQUENCE_NUM *
                  self.WORD_TO_BYTE_CONVERTER)
        return offset

    def _write_into_view(
            self, view_start_position, view_end_position,
            data, data_start_position, data_end_position, seq_num,
            packet_length, is_final):
        """ puts data into the view

        :param view_start_position: where in view to start
        :param view_end_position: where in view to end
        :param data: the data holder to write from
        :param data_start_position: where in data holder to start from
        :param data_end_position: where in data holder to end
        :param seq_num: the seq number to figure
        :return: 
        """
        if view_end_position > len(self._output):
            raise Exception(
                "I'm trying to add to my output data, but am trying to add "
                "outside my acceptable output positions!!!! max is {} and "
                "I received request to fill to {} for seq num {} from max "
                "seq num {} length of packet {} and final {}".format(
                    len(self._output), view_end_position, seq_num,
                    self._max_seq_num, packet_length, is_final))
        # print "view_start={} view_end={} data_start={} data_end={}".format(
        # view_start_position, view_end_position, data_start_position,
        # data_end_position)
        self._view[view_start_position: view_end_position] = \
            data[data_start_position:data_end_position]

    def _check(self, seq_nums):
        """ verifying if the seq nums are correct.

        :param seq_nums: the received seq nums
        :return: bool of true or false given if all the seq nums been received
        """
        # hand back
        seq_nums = sorted(seq_nums)
        max_needed = self.calculate_max_seq_num()
        if len(seq_nums) > max_needed:
            raise Exception(
                "I've received more data than i was expecting!!")
        if len(seq_nums) != max_needed:
            # self._print_length_of_received_seq_nums(seq_nums, max_needed)
            return False
        return True

    def calculate_max_seq_num(self):
        """ deduces the max seq num expected to be received

        :return: int of the biggest seq num expected
        """
        n_sequence_numbers = 0
        data_left = len(self._output) - (
            (self.DATA_PER_FULL_PACKET -
             self.SDP_RETRANSMISSION_HEADER_SIZE) *
            self.WORD_TO_BYTE_CONVERTER)

        extra_n_sequences = float(data_left) / float(
            self.DATA_PER_FULL_PACKET_WITH_SEQUENCE_NUM *
            self.WORD_TO_BYTE_CONVERTER)
        n_sequence_numbers += math.ceil(extra_n_sequences)
        return int(n_sequence_numbers)

    @staticmethod
    def _print_missing(seq_nums):
        """ debug printer for the missing seq nums from the pile 

        :param seq_nums: the seq nums received so far
        :rtype: None
        """
        last_seq_num = 0
        seq_nums = sorted(seq_nums)
        for seq_num in seq_nums:
            if seq_num != last_seq_num + 1:
                print "from list im missing seq num {}".format(seq_num)
            last_seq_num = seq_num

    def _print_out_packet_data(self, data):
        """ debug prints out the data from the packet

        :param data: the packet data
        :rtype: None 
        """
        reread_data = struct.unpack("<{}I".format(
            int(math.ceil(len(data) / self.WORD_TO_BYTE_CONVERTER))),
            str(data))
        print "converted data back into readable form is {}" \
            .format(reread_data)

    @staticmethod
    def _print_length_of_received_seq_nums(seq_nums, max_needed):
        """ debug helper method for figuring out if everything been received 

        :param seq_nums: seq nums received
        :param max_needed: biggest expected to have
        :rtype: None 
        """
        if len(seq_nums) != max_needed:
            print "should have received {} sequence numbers, but received " \
                  "{} sequence numbers".format(max_needed, len(seq_nums))
            return False

    @staticmethod
    def _print_packet_num_being_sent(packet_count, n_packets):
        """ debug helper for printing missing seq num packet transmission

        :param packet_count: which packet is being fired
        :param n_packets: how many packets to fire.
        :rtype: None 
        """
        print("send sdp packet with missing seq nums: {} of {}".format(
            packet_count + 1, n_packets))
