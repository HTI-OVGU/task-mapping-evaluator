#pragma once

#include "System.h"
#include <unordered_map>

class Mapping {
protected:
	struct DeviceTriplet {
		Processor const* processor;
		Memory const* memory_in;
		Memory const* memory_out;
	};

	std::unordered_map<Task*, DeviceTriplet> mapping;

public:

	void map(Task* task, Processor const* processor, Memory const* mem_in, Memory const* mem_out) {
		mapping[task] = { processor, mem_in, mem_out };
	}

	void map(Task* task, Processor const* processor) {
		map(task, processor, processor->get_default_memory(), processor->get_default_memory());
	}

    bool empty() const { return mapping.empty(); }

	virtual bool contains(Task* task) const { return mapping.contains(task); }
	virtual Processor const* get_processor(Task* task) const { return mapping.contains(task) ? mapping.at(task).processor : nullptr; }
	virtual Memory const* get_mem_in(Task* task) const { return mapping.contains(task) ? mapping.at(task).memory_in : nullptr; }
	virtual Memory const* get_mem_out(Task* task) const { return mapping.contains(task) ? mapping.at(task).memory_out : nullptr; }
};

class MappingView : public Mapping {
	Mapping const* base_mapping;
public:
	MappingView(): base_mapping(nullptr) {} // Only for copying purpose
	MappingView(Mapping const* base_mapping) : base_mapping(base_mapping) {}

	bool contains(Task* task) const { return mapping.contains(task) || base_mapping->contains(task); }
	Processor const* get_processor(Task* task) const { return mapping.contains(task) ? mapping.at(task).processor : base_mapping->get_processor(task); }
	Memory const* get_mem_in(Task* task) const { return mapping.contains(task) ? mapping.at(task).memory_in : base_mapping->get_mem_in(task); }
	Memory const* get_mem_out(Task* task) const { return mapping.contains(task) ? mapping.at(task).memory_out : base_mapping->get_mem_out(task); }

	void apply(Mapping& other) {
		for (auto& elem : mapping) {
			other.map(elem.first, elem.second.processor, elem.second.memory_in, elem.second.memory_out);
		}
	}

	void reset(Mapping const* new_base_mapping) {
		base_mapping = new_base_mapping;
		mapping.clear();
	}
};