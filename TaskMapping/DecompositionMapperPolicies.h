#pragma once

#define NOLOG

#include "System.h"
#include "Mapping.h"
#include "MappingUtility.h"
#include "GreedyMapper.h"
#include "Evaluation.h"

#include <iomanip>

template <class EP> class SeriesParallelDecompositionMapper;
typedef std::vector<Task*> SubGraphSet;
typedef std::vector<SubGraphSet> Decomposition;

// Evaluation Policy

class EvaluationPolicyBase {
protected:
	static bool map_subgraph(System const& sys, SubGraphSet const& subgraph, DevicePair const& dev_pair, Mapping& mapping) {
		bool change = false;
		for (Task* task : subgraph) {
			if (mapping.get_processor(task) != dev_pair.get_proc() && sys.is_compatible(task, dev_pair.get_proc())) {
				mapping.map(task, dev_pair.get_proc(), dev_pair.get_mem(), dev_pair.get_mem());
				change = true;
			}
		}
		return change;
	}
};

class EvaluateAll : EvaluationPolicyBase {
public:
	static void adapt_mapping(Mapping& mapping, System const& sys, std::vector<DevicePair> const& device_pairs, Decomposition const& decomposition) {
		MappingEvaluator eval(sys);
		Time cost = eval.compute_cost(mapping);
		bool change;

		std::unordered_map<SubGraphSet const*, Area> areas;
		for (SubGraphSet const& subgraph : decomposition) {
			Area area = 0;
			for (Task* task : subgraph) {
				area += task->get_area_requirement();
			}
			areas[&subgraph] = area;
		}

		std::unordered_map<Processor const*, Area> remaining_area;
		for (DevicePair const& dev_pair : device_pairs) {
			Processor const* const proc = dev_pair.get_proc();
			if (proc->has_maximum_capacity()) {
				remaining_area[proc] = proc->get_maximum_capacity();
			}
		}

#ifndef NOLOG
		size_t it_count = 0;
		size_t computed_mapping_count = 0;
#endif
		do {
			change = false;
			MappingView best_mapping;
			Time best_cost = cost;
			Processor const* best_proc = nullptr;
			Area best_area = 0;

			for (DevicePair const& dev_pair : device_pairs) {
				for (SubGraphSet const& subgraph : decomposition) {
					if (!dev_pair.get_proc()->has_maximum_capacity() || areas[&subgraph] < remaining_area[dev_pair.get_proc()])
					{
						MappingView current_mapping(&mapping);
						if (map_subgraph(sys, subgraph, dev_pair, current_mapping)) {
							Time curr_cost = eval.compute_cost(current_mapping);

#ifndef NOLOG
							++computed_mapping_count;
#endif

							if (curr_cost < best_cost) {
								best_cost = curr_cost;
								best_mapping = std::move(current_mapping);
								best_proc = dev_pair.get_proc();
								best_area = areas[&subgraph];
								change = true;
							}
						}
					}
				}
			}

			if (change) {
#ifndef NOLOG
				std::cout << "Iteration " << std::left << std::setw(4) << ++it_count << " Solution improved! New cost: " << std::setw(5) << best_cost << " Computed mappings: " << computed_mapping_count << std::endl;
#endif
				best_mapping.apply(mapping);
				cost = best_cost;

				assert(best_proc);
				if (best_proc->has_maximum_capacity()) {
					// Leads to one-way mapping, area cannot be "freed" again after it has been mapped once.
					remaining_area[best_proc] -= best_area;
				}
			}
		} while (change);
	}
};

template <int THRESHOLD_TIMES_TEN> class EvaluateThreshold : EvaluationPolicyBase {
public:
	static void adapt_mapping(Mapping& mapping, System const& sys, std::vector<DevicePair> const& device_pairs, Decomposition const& decomposition) {
		MappingEvaluator eval(sys);
		Time cost = eval.compute_cost(mapping);

		struct QueueElement {
			Time time_diff;
			DevicePair const* dev_pair;
			SubGraphSet const* subgraph;

			bool operator<(QueueElement const& other) const {
				return time_diff < other.time_diff;
			}

            QueueElement(Time const& time_diff, DevicePair const* dev_pair, SubGraphSet const* subgraph) : time_diff(time_diff), dev_pair(dev_pair), subgraph(subgraph) {}
		};

#ifndef NOLOG
		size_t it_count = 0;
		size_t computed_mapping_count = 0;
#endif

		std::priority_queue<QueueElement> effect_queue;
		std::unordered_map<SubGraphSet const*, Area> areas;

		MappingView best_mapping;
		Processor const* best_proc = nullptr;
		Area best_area = 0;
		Time best_cost = cost;
		for (SubGraphSet const& subgraph : decomposition) {
			Area area = 0;
			for (Task* task : subgraph) {
				area += task->get_area_requirement();
			}
			areas[&subgraph] = area;

			for (DevicePair const& dev_pair : device_pairs) {
				if (!dev_pair.get_proc()->has_maximum_capacity() || area <= dev_pair.get_proc()->get_maximum_capacity()) {
					MappingView current_mapping(&mapping);
					if (map_subgraph(sys, subgraph, dev_pair, current_mapping)) {
						Time curr_cost = eval.compute_cost(current_mapping);
						Time cost_diff = cost - curr_cost;

#ifndef NOLOG
						++computed_mapping_count;
#endif

						if (curr_cost < best_cost) {
							best_cost = curr_cost;
							best_mapping = std::move(current_mapping);
							best_proc = dev_pair.get_proc();
							best_area = area;
						}
						effect_queue.push({ cost_diff, &dev_pair, &subgraph });
					}
					else {
						effect_queue.push({ 0, &dev_pair, &subgraph });
					}
				}
			}
		}

		std::unordered_map<Processor const*, Area> remaining_area;
		for (DevicePair const& dev_pair : device_pairs) {
			Processor const* const proc = dev_pair.get_proc();
			if (proc->has_maximum_capacity()) {
				remaining_area[proc] = proc->get_maximum_capacity();
			}
		}

		std::vector<QueueElement> updated_elements;
		while (best_cost < cost) {
#ifndef NOLOG
			std::cout << "Iteration " << std::left << std::setw(4) << ++it_count << " Solution improved! New cost: " << std::setw(5) << best_cost << " Computed mappings: " << computed_mapping_count << std::endl;
#endif
			best_mapping.apply(mapping);
			cost = best_cost;
			assert(best_proc);
			if (best_proc->has_maximum_capacity()) {
				// Leads to one-way mapping, area cannot be "freed" again after it has been mapped once.
				remaining_area[best_proc] -= best_area;
			}

			for (QueueElement& element : updated_elements) {
				effect_queue.push(std::move(element));
			}
			updated_elements.clear();
			
			while (!effect_queue.empty()) {
				QueueElement const& element = effect_queue.top();
				
				if (cost != best_cost && (element.time_diff == std::numeric_limits<Time>::min() || cost - best_cost > THRESHOLD_TIMES_TEN / 10. * element.time_diff)) {
					break;
				}

				Time cost_diff = std::numeric_limits<Time>::min();
				if (!element.dev_pair->get_proc()->has_maximum_capacity() || areas[element.subgraph] <= remaining_area[element.dev_pair->get_proc()]) {
					MappingView current_mapping(&mapping);
					map_subgraph(sys, *element.subgraph, *element.dev_pair, current_mapping);

					Time curr_cost = eval.compute_cost(current_mapping);
					cost_diff = cost - curr_cost;

#ifndef NOLOG
					++computed_mapping_count;
#endif
					if (curr_cost < best_cost) {
						best_cost = curr_cost;
						best_mapping = std::move(current_mapping);
						best_proc = element.dev_pair->get_proc();
						best_area = areas[element.subgraph];
					}
				}

				updated_elements.emplace_back(cost_diff, element.dev_pair, element.subgraph);
				effect_queue.pop();
			};
		}
	}
};

// BaseMappingPolicy

class GreedyBase {
public:
	static Mapping create_base_mapping(System const& sys) {
		GreedyMapper mapper({ "CPU", "Main_RAM" });
		return mapper.get_task_mapping(sys);
	}
};

template <class EP> class SPDBase {
public:
	static Mapping create_base_mapping(System const& sys) {
		struct BasePolicies {
			typedef EP EvaluationPolicy;
			typedef GreedyBase BaseMappingPolicy;
		};

		SeriesParallelDecompositionMapper<BasePolicies> mapper(false);
		return mapper.get_task_mapping(sys);
	}
};