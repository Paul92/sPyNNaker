"""
DelayPartitionableEdge
"""

# spynnaker imports
from spynnaker.pyNN.models.neural_properties.synaptic_list import SynapticList
from spynnaker.pyNN.models.neural_properties.synapse_row_info \
    import SynapseRowInfo
from spynnaker.pyNN.models.neural_projections.projection_partitionable_edge \
    import ProjectionPartitionableEdge
from spynnaker.pyNN.models.neural_projections.delay_partitioned_edge \
    import DelayPartitionedEdge
from spynnaker.pyNN.utilities import conf

# spinn front end common imports
from spinn_front_end_common.utilities.timer import Timer

# pacman imports
from pacman.utilities.progress_bar import ProgressBar

# general imports
import math
import logging


logger = logging.getLogger(__name__)


class DelayPartitionableEdge(ProjectionPartitionableEdge):
    """
    DelayPartitionableEdge
    """

    def __init__(self, presynaptic_population, postsynaptic_population,
                 machine_time_step, num_delay_stages, max_delay_per_neuron,
                 connector=None, synapse_list=None, synapse_dynamics=None,
                 label=None):
        ProjectionPartitionableEdge.__init__(self,
                                             presynaptic_population,
                                             postsynaptic_population,
                                             machine_time_step,
                                             connector=connector,
                                             synapse_list=synapse_list,
                                             synapse_dynamics=synapse_dynamics,
                                             label=label)
        self._pre_vertex = presynaptic_population._internal_delay_vertex
        self._stored_synaptic_data_from_machine = None

    @property
    def num_delay_stages(self):
        """

        :return:
        """
        return self._pre_vertex.max_stages

    @property
    def max_delay_per_neuron(self):
        """

        :return:
        """
        return self._pre_vertex.max_delay_per_neuron

    def _get_delay_stage_max_n_words(self, vertex_slice, stage):
        min_delay = ((stage + 1) * self.max_delay_per_neuron) + 1
        max_delay = min_delay + self.max_delay_per_neuron
        conns = self.synapse_list.get_max_n_connections(
            vertex_slice=vertex_slice, lo_delay=min_delay, hi_delay=max_delay)
        return conns

    def get_max_n_words(self, vertex_slice=None):
        """
        Gets the maximum number of words for a subvertex at the end of the
        connection
        :param vertex_slice: the vertex slice which represents which part
                             of the partitionable vertex
        :type vertex_slice: pacman.model.graph_mapper.slide
        """
        return max([self._get_delay_stage_max_n_words(vertex_slice, stage)
                    for stage in range(self._pre_vertex.max_stages)])

    def get_n_rows(self):
        """
        Gets the number of synaptic rows coming in to a vertex at the end of
        the connection
        """
        return self._synapse_list.get_n_rows() * self._pre_vertex.max_stages

    def create_subedge(self, presubvertex, postsubvertex, constraints=None,
                       label=None):
        """
        Creates a subedge from this edge
        :param postsubvertex:
        :param presubvertex:
        :param constraints:
        :param label:
        """
        if constraints is None:
            constraints = list()
        constraints.extend(self._constraints)
        return DelayPartitionedEdge(presubvertex, postsubvertex, constraints)

    def get_synaptic_list_from_machine(self, graph_mapper, partitioned_graph,
                                       placements, transceiver, routing_infos):
        """
        Get synaptic data for all connections in this Projection from the
        machine.
        :param graph_mapper:
        :param partitioned_graph:
        :param placements:
        :param transceiver:
        :param routing_infos:
        :return:
        """
        if self._stored_synaptic_data_from_machine is None:
            logger.debug("Reading synapse data for edge between {} and {}"
                         .format(self._pre_vertex.label,
                                 self._post_vertex.label))
            timer = None
            if conf.config.getboolean("Reports", "outputTimesForSections"):
                timer = Timer()
                timer.start_timing()

            subedges = \
                graph_mapper.get_partitioned_edges_from_partitionable_edge(
                    self)
            if subedges is None:
                subedges = list()

            synaptic_list = [SynapseRowInfo([], [], [], [])
                             for _ in range(self._pre_vertex.n_atoms)]
            progress_bar = ProgressBar(
                len(subedges), "progress on reading back synaptic matrix")
            for subedge in subedges:
                n_rows = subedge.get_n_rows(graph_mapper)
                pre_vertex_slice = \
                    graph_mapper.get_subvertex_slice(subedge.pre_subvertex)
                post_vertex_slice = \
                    graph_mapper.get_subvertex_slice(subedge.post_subvertex)

                sub_edge_post_vertex = \
                    graph_mapper.get_vertex_from_subvertex(
                        subedge.post_subvertex)
                rows = sub_edge_post_vertex.get_synaptic_list_from_machine(
                    placements, transceiver, subedge.pre_subvertex, n_rows,
                    subedge.post_subvertex,
                    self._synapse_row_io, partitioned_graph,
                    routing_infos, subedge.weight_scales).get_rows()

                for i in range(len(rows)):
                    delay_stage = math.floor(
                        float(i) / float(pre_vertex_slice.n_atoms)) + 1
                    min_delay = (delay_stage *
                                 self.pre_vertex.max_delay_per_neuron)
                    synaptic_list[(i % pre_vertex_slice.n_atoms) +
                                  pre_vertex_slice.lo_atom]\
                        .append(rows[i], lo_atom=post_vertex_slice.lo_atom,
                                min_delay=min_delay)
                progress_bar.update()
            progress_bar.end()
            self._stored_synaptic_data_from_machine = SynapticList(
                synaptic_list)

            if conf.config.getboolean("Reports", "outputTimesForSections"):
                timer.take_sample()

        return self._stored_synaptic_data_from_machine
