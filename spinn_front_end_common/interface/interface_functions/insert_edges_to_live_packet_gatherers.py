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

from spinn_utilities.progress_bar import ProgressBar
from pacman.model.graphs.application import ApplicationEdge
from pacman.model.graphs.machine import MachineEdge
from spinn_front_end_common.utilities.exceptions import ConfigurationException


class InsertEdgesToLivePacketGatherers(object):
    """ Add edges from the recorded vertices to the local Live PacketGatherers.
    """

    def __call__(
            self, live_packet_gatherer_parameters, placements,
            live_packet_gatherers_to_vertex_mapping, machine,
            machine_graph, application_graph=None):
        """
        :param live_packet_gatherer_parameters: the set of parameters
        :param placements: the placements object
        :param live_packet_gatherers_to_vertex_mapping:
            the mapping of LPG parameters and the machine vertices associated\
            with it
        :param machine: the SpiNNaker machine
        :param machine_graph: the machine graph
        :param application_graph: the application graph
        :rtype: None
        """
        # pylint: disable=too-many-arguments, attribute-defined-outside-init

        # These are all contextual, and unmodified by this algorithm
        self._lpg_to_vertex = live_packet_gatherers_to_vertex_mapping
        self._machine = machine
        self._placements = placements

        progress = ProgressBar(
            live_packet_gatherer_parameters,
            string_describing_what_being_progressed=(
                "Adding edges to the machine graph between the vertices to "
                "which live output has been requested and its local Live "
                "Packet Gatherer"))

        if application_graph is None:
            for lpg_params in progress.over(live_packet_gatherer_parameters):
                # locate vertices to connect to a LPG with these params
                for vertex, p_ids in live_packet_gatherer_parameters[
                        lpg_params]:
                    self._connect_lpg_vertex_in_machine_graph(
                        machine_graph, vertex, lpg_params, p_ids)
        else:
            for lpg_params in progress.over(live_packet_gatherer_parameters):
                # locate vertices to connect to a LPG with these params
                for vertex, p_ids in live_packet_gatherer_parameters[
                        lpg_params]:
                    self._connect_lpg_vertex_in_application_graph(
                        application_graph, vertex, lpg_params, p_ids)

    def _connect_lpg_vertex_in_application_graph(
            self, app_graph, app_vertex, lpg_params, p_ids):
        """
        :param ~pacman.model.graphs.application.ApplicationGraph app_graph:
        :param ~pacman.model.graphs.application.ApplicationVertex app_vertex:
        """
        if not app_vertex.machine_vertices or not p_ids:
            return

        # flag for ensuring we don't add the edge to the app
        # graph many times
        app_edges = dict()

        # iterate through the associated machine vertices
        for m_vertex in app_vertex.machine_vertices:
            lpg = self._find_closest_live_packet_gatherer(
                m_vertex, lpg_params)

            # if not yet built the app edges, add them now
            # has to be postponed until we know the LPG
            if not app_edges:
                for partition_id in p_ids:
                    app_edge = ApplicationEdge(app_vertex, lpg.app_vertex)
                    app_graph.add_edge(app_edge, partition_id)
                    app_edges[partition_id] = app_edge

            # add a edge between the closest LPG and the app_vertex
            for partition_id in p_ids:
                app_edge = app_edges[p_ids]
                machine_edge = app_edge.create_machine_edge(
                    m_vertex, lpg, None)
                app_graph.machine_graph.add_edge(machine_edge, partition_id)

    def _connect_lpg_vertex_in_machine_graph(
            self, m_graph, m_vertex, lpg_params, p_ids):
        """
        :param ~pacman.model.graphs.machine.MachineGraph m_graph:
        :param ~pacman.model.graphs.machine.MachineVertex m_vertex:
        """
        # Find all Live Gatherer machine vertices
        lpg = self._find_closest_live_packet_gatherer(m_vertex, lpg_params)

        # add edges between the closest LPG and the vertex
        for partition_id in p_ids:
            m_graph.add_edge(MachineEdge(m_vertex, lpg), partition_id)

    def _find_closest_live_packet_gatherer(self, m_vertex, lpg_params):
        """ Locates the LPG on the nearest Ethernet-connected chip to the\
            machine vertex in question, or the LPG on 0, 0 if a closer one\
            can't be found.

        :param ~pacman.model.graphs.machine.MachineVertex m_vertex:
            the machine vertex to locate the nearest LPG to
        :param lpg_params: parameters to decide what LPG is to be used
        :return: the local LPG
        :raise ConfigurationException: if a local gatherer cannot be found
        """
        # locate location of vertex in machine
        placement = self._placements.get_placement_of_vertex(m_vertex)
        chip = self._machine.get_chip_at(placement.x, placement.y)

        # locate closest LPG
        machine_lpgs = self._lpg_to_vertex[lpg_params]
        chip_key = (chip.nearest_ethernet_x, chip.nearest_ethernet_y)
        if chip_key in machine_lpgs:
            return machine_lpgs[chip_key]

        # Fallback to the root (better than total failure)
        if (0, 0) in machine_lpgs:
            return machine_lpgs[0, 0]

        # if got through all LPG vertices and not found the right one. go BOOM
        raise ConfigurationException(
            "Cannot find a Live Packet Gatherer from {} for the vertex {}"
            " located on {}:{}".format(
                machine_lpgs, m_vertex, chip.x, chip.y))
