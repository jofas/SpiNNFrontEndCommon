from spinn_front_end_common.abstract_models.\
    abstract_machine_supports_auto_pause_and_resume import \
    AbstractMachineSupportsAutoPauseAndResume
from spinn_utilities.overrides import overrides


class MachineSupportsAutoPauseAndResume(
        AbstractMachineSupportsAutoPauseAndResume):

    @overrides(AbstractMachineSupportsAutoPauseAndResume.my_local_time_period)
    def my_local_time_period(self, simulator_time_step):
        return simulator_time_step