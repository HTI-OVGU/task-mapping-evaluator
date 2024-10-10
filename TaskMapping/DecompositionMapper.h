#pragma once

#include "Mapper.h"
#include "MappingUtility.h"
#include "DecompositionMapperPolicies.h"

template <class Policies> class DecompositionMapper : public Mapper {
	Mapper* base_mapper;

protected:
	virtual Decomposition create_decomposition(TaskGraph const& task_graph) const = 0;

public:
	Mapping get_task_mapping(System const& sys) const {
		std::vector<DevicePair> device_pairs = device_pairs_from_platform(sys.get_platform());
		Decomposition decomposition = create_decomposition(sys.get_task_graph());

		Mapping mapping = Policies::BaseMappingPolicy::create_base_mapping(sys);
		Policies::EvaluationPolicy::adapt_mapping(mapping, sys, device_pairs, decomposition);

		return mapping;
	}

private:
	void map_task(System const& sys, Task* const task, DevicePair const& dev_pair, Mapping& mapping) const {
		if (sys.is_compatible(task, dev_pair.get_proc())) {
			mapping.map(task, dev_pair.get_proc(), dev_pair.get_mem(), dev_pair.get_mem());
		}
	}
};