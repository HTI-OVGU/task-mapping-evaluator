#pragma once

#include "DecompositionMapper.h"
#include "SeriesParallelDecomposition.h"

#include <queue>

template <class Policies> class SeriesParallelDecompositionMapper : public DecompositionMapper<Policies> {
	bool map_single_tasks;
public:
	SeriesParallelDecompositionMapper(bool map_single_tasks = true): map_single_tasks(map_single_tasks) {}
protected:
	Decomposition create_decomposition(TaskGraph const& task_graph) const {		
		Decomposition decomposition;
		SeriesParallelDecomposition spdtree(task_graph);
#ifndef NDEBUG
		//spdtree.draw("SPDecompositionTree");
#endif
		std::unordered_set<size_t> existing_subgraphs;
		for (SeriesParallelOperation const* const op : spdtree.get_inner_nodes()) {
			auto [subgraph_id, subgraph] = subgraph_from_operation(op);
			if (subgraph.size() > 1 && !existing_subgraphs.contains(subgraph_id)) {
				existing_subgraphs.insert(subgraph_id);
				decomposition.push_back(std::move(subgraph));
			}
		}

		if (map_single_tasks) {
			for (Task* task : task_graph.get_tasks()) {
				decomposition.push_back({ task });
			}
		}

		return decomposition;
	}

private:
	std::pair<size_t, std::vector<Task*>> subgraph_from_operation(SeriesParallelOperation const* op) const {
		std::vector<Task*> subgraph;
		size_t subgraph_id = 0;

		auto add_to_subgraph = [&subgraph, &subgraph_id](Task* const task) {
			subgraph.push_back(task);
			subgraph_id ^= std::hash<Task*>{}(task);
		};

		std::queue<SeriesParallelOperation const*> queue;
		queue.push(op);

		if (op->get_type() == SeriesParallelOperationType::PARALLEL) {
			// Map outer tasks
			if (op->get_front()) add_to_subgraph(op->get_front());
			if (op->get_back()) add_to_subgraph(op->get_back());
		}

		while (!queue.empty()) {
			SeriesParallelOperation const* curr_op = queue.front();
			queue.pop();
			switch (curr_op->get_type()) {
				case SeriesParallelOperationType::EDGE:
					break;
				case SeriesParallelOperationType::PARALLEL:
				case SeriesParallelOperationType::SERIES:
					/* Map inner tasks */
					Task* front = curr_op->get_front();
					Task* back = curr_op->get_back();
					for (SeriesParallelOperation const* inner_op : curr_op->get_elements()) {
						if (inner_op->get_front() != front) {
							add_to_subgraph(inner_op->get_front());
						}
						if (inner_op->get_back() != back) {
							add_to_subgraph(inner_op->get_back());
						}
						queue.push(inner_op);
					}
					break;
			}
		}

		return std::make_pair(subgraph_id, subgraph);
	}
};