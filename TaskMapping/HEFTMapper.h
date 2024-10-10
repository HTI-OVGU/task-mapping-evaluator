#pragma once

#include "TaskMapperWithSchedule.h"
#include "TopologicalSorting.h"

#include <unordered_map>
#include <list>
#include <limits>
#include <cmath>

class HEFTMapper : public TaskMapperWithSchedule {
public:
	Mapping get_task_mapping(System const& sys) const {
		Mapping mapping;
		task_schedule = std::priority_queue<std::pair<Time, Task*>>();

		std::vector<Task*> const& tasks = sys.get_task_graph().get_tasks();
		std::vector<Processor*> const& processors = sys.get_platform().get_processors();

		std::unordered_map<Task*, Time> rank;

		BFSSorting topsort(sys.get_task_graph(), false);
		auto& sorted_elements = topsort.get_sorted_elements();
		for (auto rit = sorted_elements.rbegin(); rit != sorted_elements.rend(); ++rit) {
			Task* task = rit->get_task();
			Time avg_computation = 0;
			int nbr_compatible_proc = 0;
			for (Processor* proc : processors) {
				if (sys.is_compatible(task, proc)) {
					avg_computation += sys.computation_time_ms(task, proc);
					++nbr_compatible_proc;
				}
			}
			assert(nbr_compatible_proc > 0);
			avg_computation /= nbr_compatible_proc;

			Time r = 0;
			for (Task* succ : task->compute_successors()) {
				Time avg_communication = 0;
				int nbr_compatible_comm = 0;

				for (Processor* proc : processors) {
					if (sys.is_compatible(task, proc)) {
						for (Processor* succ_proc : processors) {
							if (sys.is_compatible(succ, succ_proc)) {
                                Time const trans_time = sys.transaction_time_ms(task->get_output_size(), proc->get_default_memory(), succ_proc->get_default_memory());
                                if (trans_time < std::numeric_limits<Time>::infinity()) {
                                    avg_communication +=  trans_time;
                                    ++nbr_compatible_comm;
                                }
							}
						}
					}
				}
				avg_communication /= nbr_compatible_comm;

				r = std::max(r, rank[succ] + avg_communication);
			}
			rank[task] = std::nextafter(r, std::numeric_limits<Time>::infinity()); // Guarantees that order is preserved if avg_time == 0

#ifndef NDEBUG
            for (Task* succ : task->compute_successors()) {
                assert(rank[task] > rank[succ]);
            }
#endif
		}

		std::vector<Task*> prioritized_tasks(tasks.size());
		std::partial_sort_copy(tasks.begin(), tasks.end(), prioritized_tasks.begin(), prioritized_tasks.end(), [&rank](Task* t, Task* other) {return rank[t] > rank[other];});

		std::unordered_map<Task*, Time> scheduled_finish_time;
		std::unordered_map<Processor*, std::list<std::pair<Time, Time>> > free_slots;
		std::unordered_map<Processor*, Area> remaining_area;
		for (Processor* proc : processors) {
			free_slots[proc].push_back({0, std::numeric_limits<Time>::infinity()});
			if (proc->has_maximum_capacity()) {
				remaining_area[proc] = proc->get_maximum_capacity();
			}
		}

		for (Task* task : prioritized_tasks) {			
			Processor* min_proc = nullptr;
			std::pair<Time, Time> min_slot(0, std::numeric_limits<Time>::infinity());

			for (Processor* proc : processors) {
				if (sys.is_compatible(task, proc) && (!proc->has_maximum_capacity() || task->get_area_requirement() <= remaining_area[proc])) {
					Time min_start_time = 0;
					for (Edge* e : task->get_edges_in()) {
                        assert(scheduled_finish_time.contains(e->get_src()));
						min_start_time = std::max(min_start_time, scheduled_finish_time.at(e->get_src()) + sys.transaction_time_ms(e->get_src()->get_output_size(), mapping.get_mem_out(e->get_src()), proc->get_default_memory()));
					}

                    if (min_start_time == std::numeric_limits<Time>::infinity()) {
                        continue;
                    }

					for (auto& slot : free_slots[proc]) {
						Time finish_time = std::max(min_start_time, slot.first) + sys.computation_time_ms(task, proc);
						if (finish_time <= slot.second) {
							if (finish_time < min_slot.second) {
								min_slot = { std::max(min_start_time, slot.first), finish_time };
								min_proc = proc;
							}
							break;
						}
					}
				}
			}

			task_schedule.push({ min_slot.first, task });

			assert(min_proc);
			for (auto it = free_slots[min_proc].begin(); it != free_slots[min_proc].end(); ++it) {
				if (it->second >= min_slot.second) {
					Time const tmp_first = it->first;
					it->first = min_slot.second;

					if (tmp_first != min_slot.first) {
						free_slots[min_proc].insert(it, { tmp_first, min_slot.first });
					}
					break;
				}
			}

			if (min_proc->has_maximum_capacity()) {
				remaining_area[min_proc] -= task->get_area_requirement();
			}

			scheduled_finish_time[task] = min_slot.second;
			mapping.map(task, min_proc);
		}

		return mapping;
	}
};