# Copyright (c) 2019-2020 The University of Manchester
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

import logging
import os
import sys
from collections import defaultdict
from spinn_utilities.log import FormatAdapter
from pacman.model.graphs.common import EdgeTrafficType
from spinn_front_end_common.interface.interface_functions.\
    machine_bit_field_router_compressor import (
        PROV_TOP_NAME)
from spinn_front_end_common.utilities.helpful_functions import (
    find_executable_start_type)
from spinn_front_end_common.utilities.report_functions.\
    bit_field_summary import (
        BitFieldSummary)
from spinn_front_end_common.utilities.utility_objs import ExecutableType

logger = FormatAdapter(logging.getLogger(__name__))
_FILE_NAME = "bit_field_compressed_summary.rpt"


class BitFieldCompressorReport(object):
    """ Generates a report that shows the impact of the compression of \
        bitfields into the routing table.

    :param str report_default_directory: report folder
    :param list(ProvenanceDataItem) provenance_items: prov items
    :param ~pacman.model.graphs.machine.MachineGraph machine_graph:
        the machine graph
    :param ~pacman.model.placements.Placements placements: the placements
    :return: a summary, or `None` if the report file can't be written
    :rtype: BitFieldSummary
    """
    def __call__(
            self, report_default_directory, provenance_items, machine_graph,
            placements):
        """
        :param str report_default_directory:
        :param list(ProvenanceDataItem) provenance_items:
        :param ~.MachineGraph machine_graph:
        :param ~.Placements placements:
        :rtype: BitFieldSummary or None
        """
        file_name = os.path.join(report_default_directory, _FILE_NAME)
        try:
            with open(file_name, "w") as f:
                return self._write_report(
                    f, provenance_items, machine_graph, placements)
        except IOError:
            logger.exception("Generate_placement_reports: Can't open file"
                             " {} for writing.", _FILE_NAME)
            return None

    @staticmethod
    def _merged_component(provenance_items, writer):
        """ Report how many bitfields were merged into the router.

        :param list(ProvenanceDataItem) provenance_items: prov items
        :param ~io.FileIO writer: file writer.
        :return: tuple containing 4 elements.
         1. min_bit_fields merged in a chip,
         2. the max bit_fields merged in a chip,
         3. the total bit_fields_merged into all the routers.
         4. average number of bit-fields merged on the routers.
        :rtype: tuple(int or str, int or str, int or str, float or str)
        """
        top_bit_field = 0
        min_bit_field = sys.maxsize
        total_bit_fields_merged = 0
        average_per_chip_merged = 0
        n_chips = 0

        found = False
        for prov_item in provenance_items:
            if prov_item.names[0] == PROV_TOP_NAME:
                found = True
                bits = prov_item.names[1].split("_")
                writer.write(
                    "Chip {}:{} has {} bitfields merged into it\n".format(
                        bits[3], bits[4], prov_item.value))
                total_bit_fields_merged += int(prov_item.value)
                if int(prov_item.value) > top_bit_field:
                    top_bit_field = int(prov_item.value)
                if int(prov_item.value) < min_bit_field:
                    min_bit_field = int(prov_item.value)
                average_per_chip_merged += int(prov_item.value)
                n_chips += 1

        if found:
            average_per_chip_merged = \
                float(average_per_chip_merged) / float(n_chips)
        else:
            min_bit_field = "N/A"
            top_bit_field = "N/A"
            total_bit_fields_merged = "N/A"
            average_per_chip_merged = "N/A"

        return (min_bit_field, top_bit_field, total_bit_fields_merged,
                average_per_chip_merged)

    @staticmethod
    def _before_merge_component(machine_graph, placements):
        """
        :param ~.MachineGraph machine_graph:
        :param ~.Placements placements:
        :rtype: tuple(int, int, int, float or int)
        """
        total_to_merge = 0
        to_merge_per_chip = defaultdict(int)

        for placement in placements:
            binary_start_type = find_executable_start_type(placement.vertex)

            if binary_start_type != ExecutableType.SYSTEM:
                seen_partitions = set()
                for incoming_edge in machine_graph.get_edges_ending_at_vertex(
                        placement.vertex):
                    if incoming_edge.traffic_type == EdgeTrafficType.MULTICAST:
                        incoming_partition = \
                            machine_graph.get_outgoing_partition_for_edge(
                                incoming_edge)
                        if incoming_partition not in seen_partitions:
                            total_to_merge += 1
                            to_merge_per_chip[placement.x, placement.y] += 1
                            seen_partitions.add(incoming_partition)

        max_bit_fields_on_chip = 0
        min_bit_fields_on_chip = sys.maxsize

        for bitfield_count in to_merge_per_chip.values():
            if bitfield_count > max_bit_fields_on_chip:
                max_bit_fields_on_chip = bitfield_count
            if bitfield_count < min_bit_fields_on_chip:
                min_bit_fields_on_chip = bitfield_count

        if len(to_merge_per_chip) == 0:
            average = 0
        else:
            average = float(total_to_merge) / float(len(to_merge_per_chip))

        return (total_to_merge, max_bit_fields_on_chip,
                min_bit_fields_on_chip, average)

    def _write_report(
            self, writer, provenance_items, machine_graph, placements):
        """ writes the report

        :param ~io.FileIO writer: the file writer
        :param list(ProvenanceDataItem) provenance_items: the prov items
        :param ~.MachineGraph machine_graph: the machine graph
        :param ~.Placements placements: the placements
        :return: a summary
        :rtype: BitFieldSummary
        """

        (min_bit_field, top_bit_field, total_bit_fields_merged,
         average_per_chip_merged) = self._merged_component(
            provenance_items, writer)
        (total_to_merge, max_to_merge_per_chip, low_to_merge_per_chip,
         average_per_chip_to_merge) = self._before_merge_component(
            machine_graph, placements)

        writer.write(
            "\n\nSummary: merged total {} out of {} bitfields. best per chip "
            "was {} and the most on one chip was {}. worst"
            " per chip was {} where the least on a chip was {}. average over "
            "all chips is {} out of {}".format(
                total_bit_fields_merged, total_to_merge, top_bit_field,
                max_to_merge_per_chip, min_bit_field, low_to_merge_per_chip,
                average_per_chip_merged, average_per_chip_to_merge))

        return BitFieldSummary(
            lowest_per_chip=min_bit_field, max_per_chip=top_bit_field,
            total_merged=total_bit_fields_merged,
            total_to_merge=total_to_merge,
            max_to_merge_per_chip=max_to_merge_per_chip,
            low_to_merge_per_chip=low_to_merge_per_chip,
            average_per_chip_merged=average_per_chip_merged,
            average_per_chip_to_merge=average_per_chip_to_merge)
