# spinn_machine imports
import logging

from spinn_machine import VirtualMachine
from spinn_utilities.log import FormatAdapter

logger = FormatAdapter(logging.getLogger(__name__))


class VirtualMachineGenerator(object):
    """ Generates a virtual machine with given dimensions and configuration.
    """

    __slots__ = []

    def __call__(
            self, width=None, height=None, virtual_has_wrap_arounds=None,
            version=None, n_cpus_per_chip=18, with_monitors=True,
            down_chips=None, down_cores=None, down_links=None,
            max_sdram_size=None):
        """
        :param width: The width of the machine in chips
        :param height: The height of the machine in chips
        :param virtual_has_wrap_arounds: \
            True if the machine should be created with wrap_arounds
        :param version: The version of board to create
        :param n_cpus_per_chip: The number of cores to put on each chip
        :param with_monitors: If true, CPU 0 will be marked as a monitor
        :param down_chips: The set of chips that should be considered broken
        :param down_cores: The set of cores that should be considered broken
        :param down_links: The set of links that should be considered broken
        :param max_sdram_size: The SDRAM that should be given to each chip
        """
        # pylint: disable=too-many-arguments
        machine = VirtualMachine(
            width=width, height=height,
            with_wrap_arounds=virtual_has_wrap_arounds,
            version=version, n_cpus_per_chip=n_cpus_per_chip,
            with_monitors=with_monitors, down_chips=down_chips,
            down_cores=down_cores, down_links=down_links,
            sdram_per_chip=max_sdram_size)

        # Work out and add the SpiNNaker links and FPGA links
        machine.add_spinnaker_links(version)
        machine.add_fpga_links(version)

        logger.info(
            "Created a virtual machine which has {}".format(
                machine.cores_and_link_output_string()))

        return machine
