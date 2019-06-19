import logging
from spinn_utilities.log import FormatAdapter
from spinn_machine import json_machine, virtual_machine, Machine, Router


logger = FormatAdapter(logging.getLogger(__name__))
from spinn_machine import virtual_machine, Machine, Router


class VirtualMachineGenerator(object):
    """ Generates a virtual machine with given dimensions and configuration.
    """

    __slots__ = []

    def __call__(
            self, width=None, height=None, virtual_has_wrap_arounds=None,
            version=None, n_cpus_per_chip=Machine.MAX_CORES_PER_CHIP,
            with_monitors=True, down_chips=None, down_cores=None,
            down_links=None, max_sdram_size=None,
            router_entries_per_chip=Router.ROUTER_DEFAULT_AVAILABLE_ENTRIES,
            json_path=None):
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

        if json_path is None:
           # pylint: disable=too-many-arguments
            machine = virtual_machine(
                width=width, height=height,
                with_wrap_arounds=virtual_has_wrap_arounds,
                version=version, n_cpus_per_chip=n_cpus_per_chip,
                with_monitors=with_monitors, down_chips=down_chips,
                down_cores=down_cores, down_links=down_links,
                sdram_per_chip=max_sdram_size,
                router_entries_per_chip=router_entries_per_chip, validate=True)
        else:
            if (height is not None or width is not None or
                    virtual_has_wrap_arounds is not None or
                    version is not None or down_chips is not None or
                    down_cores is not None or down_links is not None):
                logger.warning("As json_path specified all other virtual "
                               "machine settings ignored.")
            machine = json_machine(json_path)

        # Work out and add the SpiNNaker links and FPGA links
        machine.add_spinnaker_links()
        machine.add_fpga_links()

        return machine
