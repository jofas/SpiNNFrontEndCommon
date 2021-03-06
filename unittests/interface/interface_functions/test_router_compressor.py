# Copyright (c) 2017-2019 The University of Manchester
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

from spinn_machine.virtual_machine import virtual_machine
from spinnman.model.enums import CPUState
from spinnman.model import IOBuffer
from spinnman.utilities.appid_tracker import AppIdTracker
from pacman.model.routing_tables import (
    MulticastRoutingTables, UnCompressedMulticastRoutingTable)
from spinn_front_end_common.interface.interface_functions.\
    on_chip_router_table_compression.compression import (
        mundy_on_chip_router_compression)


class MockTransceiverError(object):

    def __init__(self):
        self.app_id_tracker = AppIdTracker()

    def malloc_sdram(self, x, y, size, app_id, tag=None):
        # Always return 0 as doesn't matter here, because the write is also
        # mocked and does nothing
        return 0

    def write_memory(
            self, x, y, base_address, data, n_bytes=None, offset=0, cpu=0,
            is_filename=False):
        # Do nothing as it isn't really going to run
        pass

    def execute_application(self, executable_targets, app_id):
        # Do nothing as it isn't really going to run
        pass

    def wait_for_cores_to_be_in_state(
            self, all_core_subsets, app_id, cpu_states, timeout=None,
            time_between_polls=0.1,
            error_states=frozenset(
                {CPUState.RUN_TIME_EXCEPTION, CPUState.WATCHDOG}),
            counts_between_full_check=100, progress_bar=None):
        # Return immediately
        pass

    def get_iobuf(self, core_subsets=None):
        # Yield a fake iobuf
        for core_subset in core_subsets:
            x = core_subset.x
            y = core_subset.y
            for p in core_subset.processor_ids:
                yield IOBuffer(x, y, p, "[ERROR] (Test): Compression Failed")

    def stop_application(self, app_id):
        # No need to stop nothing!
        pass

    def read_user_0(self, x, y, p):
        return 0


def test_router_compressor_on_error():
    routing_tables = MulticastRoutingTables(
        [UnCompressedMulticastRoutingTable(0, 0)])
    transceiver = MockTransceiverError()
    machine = virtual_machine(width=8, height=8)
    mundy_on_chip_router_compression(
        routing_tables, transceiver, machine, app_id=17,
        system_provenance_folder="")
