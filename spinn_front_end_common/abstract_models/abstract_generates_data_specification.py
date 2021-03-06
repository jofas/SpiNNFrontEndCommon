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

from six import add_metaclass
from spinn_utilities.abstract_base import AbstractBase, abstractmethod


@add_metaclass(AbstractBase)
class AbstractGeneratesDataSpecification(object):

    __slots__ = ()

    @abstractmethod
    def generate_data_specification(self, spec, placement):
        """ Generate a data specification.

        :param ~data_specification.DataSpecificationGenerator spec:
            The data specification to write to
        :param ~pacman.model.placements.Placement placement:
            The placement the vertex is located at
        :rtype: None
        """
