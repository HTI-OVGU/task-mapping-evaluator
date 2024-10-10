#pragma once

#include "TaskMapperWithSchedule.h"
#include "TopologicalSorting.h"

#include <unordered_map>
#include <list>

class PEFTMapper : public TaskMapperWithSchedule {
public:
	Mapping get_task_mapping(System const& sys) const {
		Mapping mapping;
		task_schedule = std::priority_queue<std::pair<Time, Task*>>();

		std::vector<Processor*> const& processors = sys.get_platform().get_processors();

		std::unordered_map<Task*, std::unordered_map<Processor*, Time>> OCT;
		std::unordered_map<Task*, Time> rank;
        std::unordered_map<Task*, int> dependencies;

        std::priority_queue<std::pair<Time, Task*>> ready_list;

		BFSSorting topsort(sys.get_task_graph(), false);
		auto& sorted_elements = topsort.get_sorted_elements();
		for (auto rit = sorted_elements.rbegin(); rit != sorted_elements.rend(); ++rit) {
			Task* task = rit->get_task();
			auto& OCTtask = OCT[task];

			Time r = 0;
			int nbr_compatible_proc = 0;

			for (Processor* proc : processors) {
				if (sys.is_compatible(task, proc)) {
					Time max_succ = 0;
					for (Task* succ : task->compute_successors()) {
						Time min_proc = std::numeric_limits<Time>::infinity();
						for (Processor* succ_proc : processors) {
							if (sys.is_compatible(succ, succ_proc)) {
								min_proc = std::min(min_proc, OCT[succ][succ_proc] + sys.computation_time_ms(succ, succ_proc) + sys.transaction_time_ms(task->get_output_size(), proc->get_default_memory(), succ_proc->get_default_memory()));
							}
						}
						max_succ = std::max(max_succ, min_proc);
					}

					OCTtask[proc] = max_succ;
					r += max_succ;
					++nbr_compatible_proc;
				}
				else {
					OCTtask[proc] = std::numeric_limits<Time>::infinity();
				}
			}

			rank[task] = r / nbr_compatible_proc;

            if (task->get_edges_in().size() > 1) {
                dependencies[task] = task->get_edges_in().size();
            } else if (task->get_edges_in().size() == 0) {
                ready_list.push({ rank[task], task });
            }
		}

		std::unordered_map<Task*, Time> scheduled_finish_time;
		std::unordered_map<Processor*, std::list<std::pair<Time, Time>> > free_slots;
		std::unordered_map<Processor*, Area> remaining_area;
		for (Processor* proc : processors) {
			free_slots[proc].push_back({ 0, std::numeric_limits<Time>::infinity() });
			if (proc->has_maximum_capacity()) {
				remaining_area[proc] = proc->get_maximum_capacity();
			}
		}

		while (!ready_list.empty()) {
			Task* task = ready_list.top().second;
			Processor* min_proc = nullptr;
			std::pair<Time, Time> min_slot(0, std::numeric_limits<Time>::infinity());
			Time min_oeft = std::numeric_limits<Time>::infinity();

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
							Time const oeft = finish_time + OCT[task][proc];
							if (oeft < min_oeft) {
								min_slot = { std::max(min_start_time, slot.first), finish_time };
								min_proc = proc;
								min_oeft = oeft;
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

			ready_list.pop();
			for (Task* succ : task->compute_successors()) {
				if (!dependencies.contains(succ) || dependencies.at(succ) == 1) {
					ready_list.push({ rank[succ],succ });
                } else {
                    --dependencies[succ];
                }
			}
		}

		return mapping;
	}
};