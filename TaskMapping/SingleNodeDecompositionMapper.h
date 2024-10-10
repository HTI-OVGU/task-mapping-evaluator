#pragma once

#include "DecompositionMapper.h"

template <class Policies> class SingleNodeDecompositionMapper : public DecompositionMapper<Policies> {
protected:
	Decomposition create_decomposition(TaskGraph const& task_graph) const {
		Decomposition decomposition;

		for (Task* task : task_graph.get_tasks()) {
			decomposition.push_back({ task });
		}

		return decomposition;
	}
};