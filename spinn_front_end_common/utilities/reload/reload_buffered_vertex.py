"""
ReloadBufferedVertex
"""

# front end common imports
from spinn_front_end_common.interface.buffer_management.buffer_models.\
    sends_buffers_from_host_partitioned_vertex_pre_buffered_impl import \
    SendsBuffersFromHostPartitionedVertexPreBufferedImpl
from spinn_front_end_common.interface.buffer_management.storage_objects.\
    buffered_sending_region import BufferedSendingRegion
from spinn_front_end_common.utilities import constants
from spinn_front_end_common.interface.buffer_management.buffer_manager \
    import BufferManager

# spinnman imports
from spinnman.messages.eieio.command_messages.event_stop_request \
    import EventStopRequest


_MAX_MEMORY_USAGE = constants.MAX_SIZE_OF_BUFFERED_REGION_ON_CHIP


class ReloadBufferedVertex(
        SendsBuffersFromHostPartitionedVertexPreBufferedImpl):
    """
    A class to work for buffered stuff for relaod purposes
    """

    def __init__(self, label, region_files_tuples):
        """
        :param label: The label of the vertex
        :param region_files_dict: A dictionary of region id -> file name
        """
        self._label = label

        self._send_buffers = dict()
        for (region_id, filename, max_size_of_buffer) in region_files_tuples:
            send_buffer = BufferedSendingRegion(max_size_of_buffer)
            reader = open(filename, "r")
            line = reader.readline()
            while line != "":
                bits = line.split(":")
                send_buffer.add_key(int(bits[0]), int(bits[1]))
                line = reader.readline()
            self._send_buffers[region_id] = send_buffer
        SendsBuffersFromHostPartitionedVertexPreBufferedImpl.__init__(
            self, self._send_buffers)
