from pacman.model.resources import SpecificChipSDRAMResource, CoreResource, \
    PreAllocatedResourceContainer
from pacman.model.resources.specific_board_iptag_resource import \
    SpecificBoardTagResource
from spinn_front_end_common.utility_models.\
    extra_monitor_support_machine_vertex import \
    ExtraMonitorSupportMachineVertex
from spinn_utilities.progress_bar import ProgressBar
from spinnman.connections.udp_packet_connections import UDPConnection


class PreAllocateResourcesForExtraMonitorSupport(object):

    def __call__(
            self, machine, pre_allocated_resources=None,
            n_cores_to_allocate=1):
        """

        :param machine: spinnaker machine object
        :param pre_allocated_resources: resources already pre allocated
        :param n_cores_to_allocate: config params for how many gatherers to use
        """

        progress = ProgressBar(
            len(list(machine.ethernet_connected_chips)) +
            len(list(machine.chips)),
            "Pre allocating resources for Extra Monitor support vertices")

        connection_mapping = dict()

        sdrams = list()
        cores = list()
        tags = list()

        # add resource requirements for re-injector and reader for data
        # extractor
        self._handle_second_monitor_support(
            cores, machine, progress, connection_mapping, tags, sdrams)

        # create pre allocated resource container
        extra_monitor_pre_allocations = PreAllocatedResourceContainer(
            specific_sdram_usage=sdrams, core_resources=cores,
            specific_iptag_resources=tags)

        # add other pre allocated resources
        if pre_allocated_resources is not None:
            extra_monitor_pre_allocations.extend(pre_allocated_resources)

        # return pre allocated resources
        return extra_monitor_pre_allocations, connection_mapping

    @staticmethod
    def _handle_second_monitor_support(
            cores, machine, progress, connection_mapping, tags, sdrams):
        """ adds the second monitor pre allocations, which reflect the 
        re-injector and data extractor support
        
        :param cores: the storage of core requirements
        :param machine: the spinnMachine instance
        :param progress: the progress bar to operate one 
        :rtype: None 
        """
        for chip in progress.over(list(machine.chips)):

            # determine connection if needed
            connection = None
            if chip.ip_address is not None:
                connection = UDPConnection(local_host=None)
                connection_mapping[(chip.x, chip.y)] = connection

            # acquire resoures
            resources = ExtraMonitorSupportMachineVertex.\
                static_resources_required(connection)

            # update data objects
            cores.append(CoreResource(chip=chip, n_cores=1))
            sdrams.append(SpecificChipSDRAMResource(
                chip=chip, sdram_usage=resources.sdram.get_value()))

            # only add tag if the vertex resides on a ethernet connected chip
            if chip.ip_address is not None:
                tags.append(SpecificBoardTagResource(
                    board=chip.ip_address,
                    ip_address=resources.iptags[0].ip_address,
                    port=resources.iptags[0].port,
                    strip_sdp=resources.iptags[0].strip_sdp,
                    tag=resources.iptags[0].tag,
                    traffic_identifier=resources.iptags[0].traffic_identifier))
