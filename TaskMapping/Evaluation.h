#pragma once

#include "System.h"
#include "Mapping.h"
#include "TopologicalSorting.h"
#include "EvaluationLog.h"

#include <unordered_map>
#include <iostream>

enum class SORTING_MODE { RANDOM, BREADTH_FIRST_SEARCH, TASK_FIRST_BFS, MAPPING_BASED };

class MappingEvaluator {
	System const& sys;
	mutable EvaluationLog log;
	mutable TopologicalSorting* cached_sorting = nullptr;
	mutable SORTING_MODE cached_mode = SORTING_MODE::TASK_FIRST_BFS;
	bool log_results;

	void set_cache(TopologicalSorting const* sorting, SORTING_MODE const& mode) const {
		if (cached_sorting) delete cached_sorting;
		cached_sorting = new CachedSorting(sorting);
		cached_mode = mode;
	}
public:
	MappingEvaluator(System const& sys, bool log_results = false) : sys(sys), log_results(log_results) {}
	~MappingEvaluator() { if (cached_sorting) delete cached_sorting; }
	
	EvaluationLog const& get_log() const { return log; }
	System const& get_sys() const { return sys; }

	bool is_compatible(Mapping const& mapping, Task** out_task = nullptr) const {
		for (Task* task : sys.get_task_graph().get_tasks()) {
			if (!sys.is_compatible(task, mapping.get_processor(task))) {
				if (out_task) *out_task = task;
				return false;
			}
		}
		return true;
	}

	bool is_complete(Mapping const& mapping, Task** out_task = nullptr) const {
		for (Task* task : sys.get_task_graph().get_tasks()) {
			if (!mapping.get_processor(task) || !mapping.get_mem_in(task) || !mapping.get_mem_out(task)) {
				if (out_task) *out_task = task;
				return false;
			}
		}
		return true;
	}

	bool satisfies_capacity_constraint(Mapping const& mapping, Processor const** out_proc = nullptr) const {
		for (Processor const* processor : sys.get_platform().get_processors()) {
			Area capacity = processor->get_maximum_capacity();
			if (capacity < std::numeric_limits<Area>::infinity()) {
				for (Task* task : sys.get_task_graph().get_tasks()) {
					if (mapping.get_processor(task) == processor) {
						capacity -= task->get_area_requirement();
					}
				}
				if (capacity < 0) {
					if (out_proc) *out_proc = processor;
					return false;
				}
			}
		}
		return true;
	}

	Time compute_cost(Mapping const& mapping, SORTING_MODE mode = SORTING_MODE::TASK_FIRST_BFS) const {

        TopologicalSorting* sorting;

		if (cached_sorting && cached_mode == mode) {
			sorting = new CachedSorting(cached_sorting);
		}
		else {
			switch (mode) {
				case SORTING_MODE::RANDOM:
					sorting = new RandomSorting(sys.get_task_graph());
					break;
				case SORTING_MODE::TASK_FIRST_BFS:
					sorting = new TaskFirstBFSSorting(sys.get_task_graph());
					set_cache(sorting, mode);
					break;
				case SORTING_MODE::MAPPING_BASED:
					sorting = new MappingBasedSorting(sys, mapping);
					break;
				case SORTING_MODE::BREADTH_FIRST_SEARCH:
					[[fallthrough]];
				default:
					sorting = new BFSSorting(sys.get_task_graph());
					set_cache(sorting, mode);
			}
		}

		for (Processor* proc : sys.get_platform().get_processors()) {
			if (proc->is_streaming_device()) {
				for (Task* task : sys.get_task_graph().get_tasks()) {
					if (mapping.get_processor(task) == proc) {
						sorting->compress_streamable_subtrees(mapping, proc);
						break;
					}
				}
			}
		}

		Time result = compute_cost_with_sorting(mapping, *sorting);

        delete sorting;
        sorting = nullptr;

		return result;
	}

	Time compute_cost_with_sorting(Mapping const& mapping, TopologicalSorting const& sorting) const {
		std::vector<GraphElement> const& sorted_elements = sorting.get_sorted_elements();

		std::unordered_map<Device const*, Time> time;
		for (Processor* proc : sys.get_platform().get_processors()) {
			time[proc] = 0;
		}
		for (Memory* mem : sys.get_platform().get_memories()) {
			time[mem] = 0;
		}

		for (GraphElement element : sorted_elements) {
			Task* next_task = element.get_task();
			if (next_task) {
				Processor const* processor = mapping.get_processor(next_task);
				Memory const* mem_in = mapping.get_mem_in(next_task);
				Memory const* mem_out = mapping.get_mem_out(next_task);

				Time const t_start = std::max({ time[processor], time[mem_in], time[mem_out] });
				Time const t_end = t_start + sys.computation_time_ms(next_task, processor) + sys.transaction_time_ms(next_task->get_input_size(), mem_in, processor) + sys.transaction_time_ms(next_task->get_output_size(), processor, mem_out);
				time[processor] = t_end;
				time[mem_in] = t_end;
				time[mem_out] = t_end;

				if (log_results) log.log(next_task, t_start, t_end);
			}

			Edge* next_edge = element.get_edge();
			if (next_edge) {
				Memory const* mem_out = mapping.get_mem_out(next_edge->get_src());
				Memory const* mem_in = mapping.get_mem_in(next_edge->get_snk());

				Time const t_start = std::max(time[mem_out], time[mem_in]);
				Time const t_end = t_start + sys.transaction_time_ms(next_edge->get_src()->get_output_size(), mem_out, mem_in);
				time[mem_out] = t_end;
				time[mem_in] = t_end;

				if (log_results) log.log(next_edge, t_start, t_end);
			}

			SubGraph* next_graph = element.get_subgraph();
			if (next_graph) {
				auto const& devices = next_graph->get_devices();
				Time const t_start = time[*std::max_element(devices.begin(), devices.end(), [&time](Device const* dev1, Device const* dev2) { return time[dev1] < time[dev2]; })];

				Time execution_time = 0;
				for (Task* task : next_graph->get_tasks()) {
					execution_time = std::max(execution_time, sys.computation_time_ms(task, mapping.get_processor(task)));
					execution_time = std::max(execution_time, sys.transaction_time_ms(task->get_input_size(), mapping.get_mem_in(task), mapping.get_processor(task)));
					execution_time = std::max(execution_time, sys.transaction_time_ms(task->get_output_size(), mapping.get_processor(task), mapping.get_mem_out(task)));
				}

				for (Edge* edge : next_graph->get_edges()) {
					execution_time = std::max(execution_time, sys.transaction_time_ms(edge->get_src()->get_output_size(), mapping.get_mem_out(edge->get_src()), mapping.get_mem_in(edge->get_snk())));
				}

				Time const t_end = t_start + execution_time;

				for (Device const* device : devices) {
					time[device] = t_end;
				}

				if (log_results) {
					for (Task* task : next_graph->get_tasks()) {
						log.log(task, t_start, t_end);
					}

					for (Edge* edge : next_graph->get_edges()) {
						log.log(edge, t_start, t_end);
					}
				}
			}
		}

		Time result = 0;
		for (auto& it : time) {
			result = std::max(result, it.second);
		}
		return result;
	}

	Time evaluate_mapping_with_check(Mapping const& mapping, int runs = 1) {
		Task* dbg_task;
		if (!is_complete(mapping, &dbg_task)) {
			std::cerr << "Mapping incomplete. Missing value for task " << dbg_task->get_label() << std::endl;
			return -1;
		}
		if (!is_compatible(mapping, &dbg_task)) {
			std::cerr << "Mapping invalid. Incompatible processor for task " << dbg_task->get_label() << std::endl;
			return -1;
		}
		Processor const* dbg_processor;
		if (!satisfies_capacity_constraint(mapping, &dbg_processor)) {
			std::cerr << "Mapping invalid. Not enough capacity for " << dbg_processor->get_label() << std::endl;
			return -1;
		}

        if (runs > 1) {
            Time min_cost = std::numeric_limits<Time>::max();
            EvaluationLog min_log;

            min_cost = compute_cost(mapping);
            min_log = log;

            /*{
                Time cost = compute_cost(mapping, SORTING_MODE::MAPPING_BASED);
                if (cost < min_cost) {
                    min_cost = cost;
                    min_log = log;
                }
            }*/

            for (int i = 1; i < runs; ++i) {
                Time cost = compute_cost(mapping, SORTING_MODE::RANDOM);
                if (cost < min_cost) {
                    min_cost = cost;
                    min_log = log;
                }
            }
            log = min_log;
            return min_cost;
        }

		return compute_cost(mapping);
	}
};