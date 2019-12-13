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

import unittest
from spinn_machine.tags import IPTag, ReverseIPTag
from pacman.model.tags import Tags
from pacman.model.graphs.machine import MachineVertex
from pacman.model.resources import ResourceContainer
from spinn_front_end_common.interface.interface_functions import TagsLoader
from spinn_front_end_common.utilities.globals_variables import get_simulator


class _MockTransceiver(object):

    def __init__(self):
        self._ip_tags = list()
        self._reverse_ip_tags = list()

    def set_ip_tag(self, ip_tag):
        self._ip_tags.append(ip_tag)

    def set_reverse_ip_tag(self, reverse_ip_tag):
        self._reverse_ip_tags.append(reverse_ip_tag)

    def clear_ip_tag(self, tag):
        pass

    @property
    def ip_tags(self):
        return self._ip_tags

    @property
    def reverse_ip_tags(self):
        return self._reverse_ip_tags


class _TestVertex(MachineVertex):
    def resources_required(self):
        return ResourceContainer(0)


class TestFrontEndCommonTagsLoader(unittest.TestCase):

    def test_call(self):
        """ Test calling the tags loader
        """

        vertex = _TestVertex(timestep_in_us=get_simulator().user_time_step_in_us)

        tag_1 = IPTag("127.0.0.1", 0, 0, 1, "localhost", 12345, True, "Test")
        tag_2 = IPTag("127.0.0.1", 0, 0, 2, "localhost", 54321, True, "Test")
        rip_tag_1 = ReverseIPTag("127.0.0.1", 3, 12345, 0, 0, 0, 0)
        rip_tag_2 = ReverseIPTag("127.0.0.1", 4, 12346, 0, 0, 0, 0)

        tags = Tags()
        tags.add_ip_tag(tag_1, vertex)
        tags.add_ip_tag(tag_2, vertex)
        tags.add_reverse_ip_tag(rip_tag_1, vertex)
        tags.add_reverse_ip_tag(rip_tag_2, vertex)

        txrx = _MockTransceiver()

        loader = TagsLoader()
        loader.__call__(txrx, tags)
        self.assertIn(tag_1, txrx.ip_tags)
        self.assertIn(tag_2, txrx.ip_tags)
        self.assertIn(rip_tag_1, txrx.reverse_ip_tags)
        self.assertIn(rip_tag_2, txrx.reverse_ip_tags)


if __name__ == '__main__':
    unittest.main()
