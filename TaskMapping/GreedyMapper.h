#pragma once

#include "Mapper.h"

class GreedyMapper : public Mapper {
	std::vector<std::string> allowed_labels;

public:
	GreedyMapper(std::vector<std::string>&& labels = {}) : allowed_labels(std::move(labels)) {};

	Mapping get_task_mapping(System const& system) const {

		std::vector<Task*> const& tasks = system.get_task_graph().get_tasks();
		std::vector<Processor*> const& processors = system.get_platform().get_processors();
		std::vector<Memory*> const& memories = system.get_platform().get_memories();

		Mapping mapping;
		Processor const* compatible_processor = nullptr;
		Memory const* compatible_memory = nullptr;
		for (Task* task : tasks) {
			if (allowed_labels.empty()) {
				for (Processor* processor : processors) {
					if (system.is_compatible(task, processor)) {
						compatible_processor = processor;
						break;
					}
				}
				for (Memory* memory : memories) {
					if (system.is_compatible(task, memory)) {
						compatible_memory = memory;
						break;
					}
				}
			}
			else {
				for (std::string const& label : allowed_labels) {
					if (compatible_processor) break;
					for (Processor* processor : processors) {
						if (processor->get_label() == label) {
							if (system.is_compatible(task, processor)) {
								compatible_processor = processor;
							}
							break;
						}
					}
				}
				for (std::string const& label : allowed_labels) {
					if (compatible_memory) break;
					for (Memory* memory : memories) {
						if (memory->get_label() == label) {
							if (system.is_compatible(task, memory)) {
								compatible_memory = memory;
							}
							break;
						}
					}
				}
			}

			mapping.map(task, compatible_processor, compatible_memory, compatible_memory);
			compatible_processor = nullptr;
			compatible_memory = nullptr;
		}
		return mapping;
	};
};