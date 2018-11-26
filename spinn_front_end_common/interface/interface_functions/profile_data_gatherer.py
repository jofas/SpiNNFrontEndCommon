from spinn_utilities.progress_bar import ProgressBar

# front end common imports
from spinn_front_end_common.interface.profiling import AbstractHasProfileData

import os
import logging
import numpy as np


logger = logging.getLogger(__name__)


class ProfileDataGatherer(object):
    __slots__ = []

    def __call__(
            self, transceiver, placements, provenance_file_path,
            run_time_ms, machine_time_step):
        """
        :param transceiver: the SpiNNMan interface object
        :param placements: The placements of the vertices
        :param has_ran: token that states that the simulation has ran
        :param provenance_file_path: The location to store the profile data
        :param run_time_ms: runtime in ms
        :param machine_time_step: machine time step in ms
        """
        # pylint: disable=too-many-arguments
        machine_time_step_ms = machine_time_step // 1000

        progress = ProgressBar(
            placements.n_placements, "Getting profile data")

        # retrieve provenance data from any cores that provide data
        for placement in progress.over(placements.placements):
            if isinstance(placement.vertex, AbstractHasProfileData):
                # get data
                profile_data = placement.vertex.get_profile_data(
                    transceiver, placement)
                if profile_data.tags:
                    self._write(placement, profile_data, run_time_ms,
                                machine_time_step_ms, provenance_file_path)

    def _write(self, p, profile_data, run_time_ms,
               machine_time_step_ms, directory):
        import numpy as np
        if 'target_pop' in p.vertex.label:
            master_profiles = np.load('/home/rjames/SpiNNaker_devel/PyNN8Examples/examples/master_profiles.npz')
            n_calls = np.append(master_profiles['n_calls'],(profile_data.get_n_calls('INCOMING_SPIKE'),profile_data.get_n_calls('DMA_READ')))
            # spike_pro = np.append(master_profiles['spike_processing'],profile_data.get_mean_ms('INCOMING_SPIKE')*profile_data.get_n_calls('INCOMING_SPIKE'))
            spike_pro = np.append(master_profiles['spike_processing'],profile_data.get_complete_profile('INCOMING_SPIKE'))
            # dma_pro = np.append(master_profiles['dma_processing'],profile_data.get_mean_ms('DMA_READ')*profile_data.get_n_calls('DMA_READ'))
            dma_pro = np.append(master_profiles['dma_processing'],profile_data.get_complete_profile('DMA_READ'))
            np.savez('/home/rjames/SpiNNaker_devel/PyNN8Examples/examples/master_profiles.npz',spike_processing=spike_pro,dma_processing=dma_pro,n_calls=n_calls)
        # pylint: disable=too-many-arguments
        max_tag_len = max([len(tag) for tag in profile_data.tags])

        # write data
        file_name = os.path.join(
            directory, "{}_{}_{}_profile.txt".format(p.x, p.y, p.p))

        # set mode of the file based off if the file already exists
        mode = "w"
        if os.path.exists(file_name):
            mode = "a"

        # write profile data to file
        with open(file_name, mode) as f:
            f.write("{: <{}s} {: <7s} {: <14s} {: <14s} {: <14s} "
                    "{: <18s} {: <14s}\n".format("tag", max_tag_len,
                    "n_calls", "mean_ms","n_calls_per_ts",
                    "mean_ms_per_ts", "standard_deviation",
                    "standard_error"))
            f.write("{:-<{}s} {:-<7s} {:-<14s} {:-<14s} {:-<14s}"
                    "{: <18s} {: <14s}\n".format(
                    "", max_tag_len, "", "", "", "", "", ""))
            for tag in profile_data.tags:
                f.write("{: <{}s} {: >7d} {: >14.6f} {: >14.6f} {: >14.6f}"
                        "{: >14.6f} {: >14.6f}\n"
                        .format(
                            tag, max_tag_len,
                            profile_data.get_n_calls(tag),
                            profile_data.get_mean_ms(tag),
                            profile_data.get_mean_n_calls_per_ts(
                                tag, run_time_ms, machine_time_step_ms),
                            profile_data.get_mean_ms_per_ts(
                                tag, run_time_ms, machine_time_step_ms),
                            profile_data.get_sd(tag),
                            profile_data.get_se(tag)
                        ))
