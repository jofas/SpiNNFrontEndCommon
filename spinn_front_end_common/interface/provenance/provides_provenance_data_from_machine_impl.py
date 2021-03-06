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

import struct
from enum import Enum
from six import add_metaclass
from spinn_utilities.abstract_base import AbstractBase, abstractproperty
from data_specification.utility_calls import get_region_base_address_offset
from .abstract_provides_provenance_data_from_machine import (
    AbstractProvidesProvenanceDataFromMachine)
from spinn_front_end_common.utilities.utility_objs import ProvenanceDataItem
from spinn_front_end_common.utilities.constants import BYTES_PER_WORD
from spinn_utilities.overrides import overrides

_ONE_WORD = struct.Struct("<I")


@add_metaclass(AbstractBase)
class ProvidesProvenanceDataFromMachineImpl(
        AbstractProvidesProvenanceDataFromMachine):
    """ An implementation that gets provenance data from a region of ints on\
        the machine.
    """

    __slots__ = ()

    class PROVENANCE_DATA_ENTRIES(Enum):
        """ Entries for the provenance data generated by models using provides\
            provenance vertex.
        """
        #: The counter of transmission overflows
        TRANSMISSION_EVENT_OVERFLOW = 0
        #: The counter of the number of times the callback queue was overloaded
        CALLBACK_QUEUE_OVERLOADED = 1
        #: The counter of the number of times the DMA queue was overloaded
        DMA_QUEUE_OVERLOADED = 2
        #: Whether the timer tick has overrun at all at any point
        TIMER_TIC_HAS_OVERRUN = 3
        #: The counter of the number of times the timer tick overran
        MAX_NUMBER_OF_TIMER_TIC_OVERRUN = 4

    NUM_PROVENANCE_DATA_ENTRIES = len(PROVENANCE_DATA_ENTRIES)

    _TIMER_TICK_OVERRUN = "Times_the_timer_tic_over_ran"
    _MAX_TIMER_TICK_OVERRUN = "max_number_of_times_timer_tic_over_ran"
    _TIMES_DMA_QUEUE_OVERLOADED = "Times_the_dma_queue_was_overloaded"
    _TIMES_TRANSMISSION_SPIKES_OVERRAN = \
        "Times_the_transmission_of_spikes_overran"
    _TIMES_CALLBACK_QUEUE_OVERLOADED = \
        "Times_the_callback_queue_was_overloaded"

    @abstractproperty
    def _provenance_region_id(self):
        """
        :return: provenance_region_id
        :rtype: int
        """

    @abstractproperty
    def _n_additional_data_items(self):
        """
        :return: n_additional_data_items
        :rtype: int
        """

    def reserve_provenance_data_region(self, spec):
        """
        :param ~data_specification.DataSpecificationGenerator spec:
            The data specification being written.
        """
        spec.reserve_memory_region(
            self._provenance_region_id,
            self.get_provenance_data_size(self._n_additional_data_items),
            label="Provenance", empty=True)

    @classmethod
    def get_provenance_data_size(cls, n_additional_data_items):
        """
        :param int n_additional_data_items:
        :rtype: int
        """
        return (
            (cls.NUM_PROVENANCE_DATA_ENTRIES + n_additional_data_items)
            * BYTES_PER_WORD)

    def _get_provenance_region_address(self, transceiver, placement):
        """
        :param ~spinnman.transceiver.Transceiver transceiver:
        :param ~pacman.model.placements.Placement placement:
        :rtype: int
        """
        # Get the App Data for the core
        app_data_base_address = transceiver.get_cpu_information_from_core(
            placement.x, placement.y, placement.p).user[0]

        # Get the provenance region base address
        base_address_offset = get_region_base_address_offset(
            app_data_base_address, self._provenance_region_id)
        base_address = transceiver.read_memory(
            placement.x, placement.y, base_address_offset, BYTES_PER_WORD)
        return _ONE_WORD.unpack(base_address)[0]

    def _read_provenance_data(self, transceiver, placement):
        """
        :param ~spinnman.transceiver.Transceiver transceiver:
        :param ~pacman.model.placements.Placement placement:
        :rtype: iterable(int)
        """
        provenance_address = self._get_provenance_region_address(
            transceiver, placement)
        data = transceiver.read_memory(
            placement.x, placement.y, provenance_address,
            self.get_provenance_data_size(self._n_additional_data_items))
        return struct.unpack_from("<{}I".format(
            self.NUM_PROVENANCE_DATA_ENTRIES + self._n_additional_data_items),
            data)

    @staticmethod
    def _get_placement_details(placement):
        """
        :param ~pacman.model.placements.Placement placement:
        :rtype: tuple(str,int,int,list(str))
        """
        label = placement.vertex.label
        x = placement.x
        y = placement.y
        p = placement.p
        names = ["vertex_{}_{}_{}_{}".format(x, y, p, label)]
        return label, x, y, p, names

    @staticmethod
    def _add_name(names, name):
        """
        :param iterable(str) names:
        :param str name:
        :rtype: list(str)
        """
        new_names = list(names)
        new_names.append(name)
        return new_names

    @staticmethod
    def _add_names(names, extra_names):
        """
        :param iterable(str) names:
        :param iterable(str) extra_names:
        :rtype: list(str)
        """
        new_names = list(names)
        new_names.extend(extra_names)
        return new_names

    def _read_basic_provenance_items(self, provenance_data, placement):
        """
        :param iterable(int) provenance_data:
        :param ~pacman.model.placements.Placement placement:
        :rtype: list(ProvenanceDataItem)
        """
        transmission_event_overflow = provenance_data[
            self.PROVENANCE_DATA_ENTRIES.TRANSMISSION_EVENT_OVERFLOW.value]
        callback_queue_overloaded = provenance_data[
            self.PROVENANCE_DATA_ENTRIES.CALLBACK_QUEUE_OVERLOADED.value]
        dma_queue_overloaded = provenance_data[
            self.PROVENANCE_DATA_ENTRIES.DMA_QUEUE_OVERLOADED.value]
        number_of_times_timer_tic_over_ran = provenance_data[
            self.PROVENANCE_DATA_ENTRIES.TIMER_TIC_HAS_OVERRUN.value]
        max_number_of_times_timer_tic_over_ran = provenance_data[
            self.PROVENANCE_DATA_ENTRIES.MAX_NUMBER_OF_TIMER_TIC_OVERRUN.value]

        # create provenance data items for returning
        label, x, y, p, names = self._get_placement_details(placement)
        data_items = list()
        data_items.append(ProvenanceDataItem(
            self._add_name(names, self._TIMES_TRANSMISSION_SPIKES_OVERRAN),
            transmission_event_overflow,
            report=transmission_event_overflow != 0,
            message=(
                "The transmission buffer for {} on {}, {}, {} was blocked "
                "on {} occasions. This is often a sign that the system is "
                "experiencing back pressure from the communication fabric. "
                "Please either: "
                "1. spread the load over more cores, "
                "2. reduce your peak transmission load,"
                "3. adjust your mapping algorithm.".format(
                    label, x, y, p, transmission_event_overflow))))

        data_items.append(ProvenanceDataItem(
            self._add_name(names, self._TIMES_CALLBACK_QUEUE_OVERLOADED),
            callback_queue_overloaded,
            report=callback_queue_overloaded != 0,
            message=(
                "The callback queue for {} on {}, {}, {} overloaded on {} "
                "occasions. This is often a sign that the system is running "
                "too quickly for the number of neurons per core.  Please "
                "increase the machine time step or time_scale_factor or "
                "decrease the number of neurons per core.".format(
                    label, x, y, p, callback_queue_overloaded))))

        data_items.append(ProvenanceDataItem(
            self._add_name(names, self._TIMES_DMA_QUEUE_OVERLOADED),
            dma_queue_overloaded,
            report=dma_queue_overloaded != 0,
            message=(
                "The DMA queue for {} on {}, {}, {} overloaded on {} "
                "occasions. This is often a sign that the system is running "
                "too quickly for the number of neurons per core.  Please "
                "increase the machine time step or time_scale_factor or "
                "decrease the number of neurons per core.".format(
                    label, x, y, p, dma_queue_overloaded))))

        data_items.append(ProvenanceDataItem(
            self._add_name(names, self._TIMER_TICK_OVERRUN),
            number_of_times_timer_tic_over_ran,
            report=number_of_times_timer_tic_over_ran != 0,
            message=(
                "A Timer tick callback was still executing when the next "
                "timer tick callback was fired off for {} on {}, {}, {}, {} "
                "times. This is a sign of the system being overloaded and "
                "therefore the results are likely incorrect.  Please increase "
                "the machine time step or time_scale_factor or decrease the "
                "number of neurons per core".format(
                    label, x, y, p, number_of_times_timer_tic_over_ran))))

        data_items.append(ProvenanceDataItem(
            self._add_name(names, self._MAX_TIMER_TICK_OVERRUN),
            max_number_of_times_timer_tic_over_ran,
            report=max_number_of_times_timer_tic_over_ran != 0,
            message=(
                "The timer for {} on {}, {}, {} fell behind by up to {} "
                "ticks. This is a sign of the system being overloaded and "
                "therefore the results are likely incorrect. Please increase "
                "the machine time step or time_scale_factor or decrease the "
                "number of neurons per core".format(
                    label, x, y, p, max_number_of_times_timer_tic_over_ran))))

        return data_items

    def _get_remaining_provenance_data_items(self, provenance_data):
        """
        :param list(ProvenanceDataItem) provenance_data:
        :rtype: list(ProvenanceDataItem)
        """
        return provenance_data[self.NUM_PROVENANCE_DATA_ENTRIES:]

    @overrides(
        AbstractProvidesProvenanceDataFromMachine.
        get_provenance_data_from_machine,
        extend_doc=False)
    def get_provenance_data_from_machine(self, transceiver, placement):
        """ Retrieve the provenance data.

        :param ~spinnman.transceiver.Transceiver transceiver:
            How to talk to the machine
        :param ~pacman.model.placements.Placement placement:
            Which vertex are we retrieving from, and where was it
        :rtype:
            list(~spinn_front_end_common.utilities.utility_objs.ProvenanceDataItem)
        """
        provenance_data = self._read_provenance_data(
            transceiver, placement)
        return self._read_basic_provenance_items(provenance_data, placement)
