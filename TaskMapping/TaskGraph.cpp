#include "TaskGraph.h"
#include <algorithm>
#include <numeric>

DataSize SUMMED_PROPAGATION(std::vector<DataSize> const& data_in) {
	return std::reduce(std::begin(data_in), std::end(data_in));
}

DataSize MAX_PROPAGATION(std::vector<DataSize> const& data_in) {
	if (data_in.size() == 0) {
		return 0;
	}
	return *std::max_element(std::begin(data_in), std::end(data_in));
}

DataSize AVERAGE_PROPAGATION(std::vector<DataSize> const& data_in) {
	if (data_in.size() == 0) {
		return 0;
	}
	return SUMMED_PROPAGATION(data_in) / static_cast<DataSize>(data_in.size());
}

DataSize DATA_SRC(std::vector<DataSize> const&) {
	//TODO make this configurable
	return 1; // MB 10000;
}

DataSize DATA_SNK(std::vector<DataSize> const&) {
	return 0;
}

void Task::compute_size() const {
	std::vector<DataSize> input_sizes;
	input_size = 0;
	for (Edge* edge : edges_in) {
		DataSize const& output_size = edge->get_src()->get_output_size();
		input_size += output_size;
		input_sizes.push_back(output_size);
	}
	output_size = size_func(input_sizes);

	dirty = false;
}

void Task::add_outgoing_edge(Edge* edge_out) {
	edges_out.push_back(edge_out);
	edge_out->get_snk()->edges_in.push_back(edge_out);

	dirty = true;
	edge_out->get_snk()->dirty = true;
}

void Task::delete_outgoing_edge(Edge* edge_out) {
	auto out_it = std::find(edges_out.begin(), edges_out.end(), edge_out);
	assert(out_it != edges_out.end());
	edges_out.erase(out_it);

	std::vector<Edge*>& succ_edges_in = edge_out->get_snk()->edges_in;
	auto in_it = std::find(succ_edges_in.begin(), succ_edges_in.end(), edge_out);
	assert(in_it != succ_edges_in.end());
	succ_edges_in.erase(in_it);

	dirty = true;
	edge_out->get_snk()->dirty = true;
}

TaskGraph::~TaskGraph() {
	clear();
}

Task* TaskGraph::add_node(ScaleFactor const& complexity, Percent const& parallelizability, ScaleFactor const& streamability, Task::SizeFuncPtr const& size_func, std::vector<Task*> predecessors, std::vector<Task*> successors) {
	Task* new_task = new Task(complexity, parallelizability, streamability, size_func);
	tasks.push_back(new_task);

	if (predecessors.size() == 0) {
		src_nodes.insert(new_task);
	}

	if (successors.size() == 0) {
		snk_nodes.insert(new_task);
	}

	for (Task* pred : predecessors) {
		add_edge(pred, new_task);
	}

	for (Task* succ : successors) {
		add_edge(new_task, succ);
	}

	return new_task;
}

void TaskGraph::add_edge(Edge const& edge) {
	add_edge(edge.get_src(), edge.get_snk());
}

void TaskGraph::add_edge(Task* src, Task* snk) {
	Edge* new_edge = new Edge(src, snk);
	edges.push_back(new_edge);

	snk_nodes.erase(src);
	src_nodes.erase(snk);

	src->add_outgoing_edge(new_edge);
}

void TaskGraph::delete_edge(Edge* edge) {
	delete_edge(edge->get_src(), edge->get_snk());
}

void TaskGraph::delete_edge(Task* csrc, Task* csnk) {
	auto it = std::find_if(edges.begin(), edges.end(), [csrc, csnk](Edge* edge) { return edge->get_src() == csrc && edge->get_snk() == csnk; });
	if (it != edges.end()) {
		Task* const src = (*it)->get_src();
		Task* const snk = (*it)->get_snk();
		
		src->delete_outgoing_edge(*it);
		if (src->get_edges_out().size() == 0) {
			snk_nodes.insert(src);
		}
		if (snk->get_edges_in().size() == 0) {
			src_nodes.insert(snk);
		}
		edges.erase(it);
	}
}
