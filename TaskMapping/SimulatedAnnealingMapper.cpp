#include "SimulatedAnnealingMapper.h"
#include "GreedyMapper.h"
#include "Evaluation.h"

#include <cmath>
#include <iomanip>

#define NO_SA_LOG

Mapping SimulatedAnnealingMapper::get_task_mapping(System const& sys) const {
	size_t const annealing_runs = 10;
	size_t const iterations_per_temperature = 50;//sys.get_task_graph().get_tasks().size()* (sys.get_platform().get_processors().size() - 1);
	Temperature const final_temperature = get_normalized_final_temperature(sys);

	GreedyMapper base_mapper({ "CPU", "Main_RAM" });
	Mapping best_mapping;
	Time best_cost = std::numeric_limits<Time>::infinity();

	for (size_t run = 0; run < annealing_runs; ++run) {
		Mapping current_best_mapping = base_mapper.get_task_mapping(sys);
	
		MappingEvaluator eval(sys);
		Time initial_cost = eval.compute_cost(current_best_mapping);
		Time current_best_cost = initial_cost;

		Temperature temperature = 1;
		MappingView curr_mapping(&current_best_mapping);
#ifndef NO_SA_LOG
		int iteration = 0;
#endif
		while (temperature > final_temperature) {
			Time curr_cost = 0;
			for (size_t i = 0; i < iterations_per_temperature; ++i) {
				MappingView new_mapping = iterate(curr_mapping, sys);
				if (!eval.satisfies_capacity_constraint(new_mapping)) {
					continue;
				}
				curr_cost = eval.compute_cost(new_mapping);
				if (curr_cost < current_best_cost || accept(curr_cost - current_best_cost, initial_cost, temperature)) {
					new_mapping.apply(curr_mapping);
					if (curr_cost < current_best_cost) {
						curr_mapping.apply(current_best_mapping);
						curr_mapping.reset(&current_best_mapping);
						current_best_cost = curr_cost;
					}
				}
			}
#ifndef NO_SA_LOG
			std::cout << "\rRun " << run << ", It " << std::setw(3) << ++iteration << " -- Cur: " << std::setw(8) << curr_cost << " Best: " << current_best_cost << " Total: " << best_cost << " Temp: " << std::setw(11) << temperature << " Final: " << final_temperature << std::flush;
#endif
			adjust_temperature(temperature);
		}

		if (current_best_cost < best_cost) {
			best_cost = current_best_cost;
			best_mapping = std::move(current_best_mapping);
		}
	}

	return best_mapping;
}

MappingView SimulatedAnnealingMapper::iterate(Mapping& curr_mapping, System const& sys) const {
	std::vector<Task*> const& tasks = sys.get_task_graph().get_tasks();
	std::vector<Processor*> const& processors = sys.get_platform().get_processors();

	Task* rand_task = tasks[rand() % tasks.size()];

	size_t proc_idx = rand() % processors.size();
	Processor* rand_proc = processors[proc_idx];

	if (rand_proc == curr_mapping.get_processor(rand_task)) {
		size_t new_idx = rand() % (processors.size() - 1);
		if (new_idx >= proc_idx) {
			++new_idx;
		}
		rand_proc = processors[new_idx];
	}

	MappingView new_mapping(&curr_mapping);

	if (sys.is_compatible(rand_task, rand_proc)) {
		new_mapping.map(rand_task, rand_proc);
	}

	return new_mapping;
}


bool SimulatedAnnealingMapper::accept(Time const& cost_diff, Time const& initial_cost, Temperature const& temperature) const {
	double const accept_threshold = std::exp(-2 * cost_diff / (temperature * initial_cost));
	return rand() % 1000 < 1000 * accept_threshold;
}

Temperature SimulatedAnnealingMapper::get_normalized_final_temperature(System const& sys) const {
	const int safety_margin_factor = 2;
	
	Time total_min_cost = 0;
	Time total_max_cost = 0;

	Time min_cost = std::numeric_limits<Time>::max();
	Time max_cost = 0;

	std::vector<Processor*> processors = sys.get_platform().get_processors();
	for (Task* task : sys.get_task_graph().get_tasks()) {
		Time curr_min_cost = std::numeric_limits<Time>::max();
		Time curr_max_cost = 0;
		for (Processor* proc : processors) {
			if (sys.is_compatible(task, proc)) {
				Time cost = sys.computation_time_ms(task, proc);
				if (cost <= 0 || cost >= std::numeric_limits<Time>::max()) {
					continue;
				}
				if (cost < curr_min_cost) {
					curr_min_cost = cost;
				}
				if (cost > curr_max_cost) {
					curr_max_cost = cost;
				}
			}
		}

		if (curr_min_cost == std::numeric_limits<Time>::max()) {
			continue;
		}

		if (curr_min_cost < min_cost) {
			min_cost = curr_min_cost;
		}

		if (curr_max_cost > max_cost) {
			max_cost = curr_max_cost;
		}

		total_min_cost += curr_min_cost;
		total_max_cost += curr_max_cost;
	}

	//Temperature const initial_temperature = safety_margin_factor * max_cost / total_min_cost;
	//Temperature const final_temperature = min_cost / (safety_margin_factor * total_max_cost);
	
	// Rearranged for numerical stability (final_temperature / initial_temperature)
	return min_cost / max_cost * total_min_cost / total_max_cost * 1. / ((Temperature)safety_margin_factor * safety_margin_factor);
}

void SimulatedAnnealingMapper::adjust_temperature(Temperature& temperature) const {
	temperature *= 0.95;
}