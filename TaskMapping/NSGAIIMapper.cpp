#include "NSGAIIMapper.h"
#include "Evaluation.h"
#include "TopologicalSorting.h"
#include "GreedyMapper.h"

#define NO_NSGA_LOG

template <class CostPolicy>
void NSGAIIMapper<CostPolicy>::init(System const& sys) const {
	for (Processor* proc : sys.get_platform().get_processors()) {
		if (proc->get_label() == "CPU") {
			default_proc = proc;
			break;
		}
	}
}

template <class CostPolicy>
Mapping NSGAIIMapper<CostPolicy>::get_task_mapping(System const& sys) const {
	size_t const constexpr POPULATION_SIZE = 100;
	init(sys);

	GreedyMapper greedy({ "CPU", "Main_RAM" });
	Mapping greedy_mapping = greedy.get_task_mapping(sys);

	MappingEvaluator eval(sys);
	std::vector<std::pair<Mapping, Time>> population;

	// Guarantee to be at least as good as the base mapping
	population.push_back(std::make_pair(greedy_mapping, CostPolicy::compute_cost(greedy_mapping, eval)));
	for (size_t i = 1; i < POPULATION_SIZE; ++i) {
		population.push_back(create_valid_random_mapping(eval));
	}

#ifndef NO_NSGA_LOG
	size_t last_change = 0;
	Time best = std::numeric_limits<Time>::infinity();
#endif

	BFSSorting sorting(sys.get_task_graph(), false);
	for (size_t i = 0; i < GENERATIONS; ++i) {
		std::vector<MappingView> parent_selection = select(population, POPULATION_SIZE * 2);
		mutate(parent_selection, sys);
		std::vector<std::pair<Mapping, Time>> new_mappings = crossover(parent_selection, sorting.get_sorted_elements(), eval);
		population.insert(population.end(), new_mappings.begin(), new_mappings.end());
		std::sort(population.begin(), population.end(), [](std::pair<Mapping, Time> const& p1, std::pair<Mapping, Time> const& p2) { return p1.second < p2.second; });
		population.resize(POPULATION_SIZE);

#ifndef NO_NSGA_LOG
		if (population.front().second < best) {
			best = population.front().second;
			last_change = i;
		}
		std::cout << "\rGeneration " << i << " -- Current cost: " << population.front().second << std::flush;
#endif
	}

#ifndef NO_NSGA_LOG
	std::cout << " - Last change at generation " << last_change+1 << " of " << GENERATIONS << std::endl;
#endif

	return population.front().first;
}

template <class CostPolicy>
std::vector<std::pair<Mapping, Time>> NSGAIIMapper<CostPolicy>::crossover(std::vector<MappingView> const& parent_selection, std::vector<GraphElement> const& sorted_tasks, MappingEvaluator const& eval) const {
	std::vector<std::pair<Mapping, Time>> new_mappings;
	
	for (size_t j = 1; j < parent_selection.size(); j = j + 2) {
		MappingView const& firstParent = parent_selection[j-1];
		MappingView const& secondParent = parent_selection[j];
		size_t crossover_point;
		// 0.1 probability to not have a crossover
		if (rand() % 10 == 0) {
			crossover_point = (rand() % 2) * sorted_tasks.size();
		}
		else {
			crossover_point = rand() % sorted_tasks.size();
		}

		Mapping new_mapping;
		for (size_t i = 0; i < sorted_tasks.size(); ++i) {
			Task* task = sorted_tasks[i].get_task();
			if (i < crossover_point) {
				new_mapping.map(task, firstParent.get_processor(task));
			}
			else {
				new_mapping.map(task, secondParent.get_processor(task));
			}
		}

		new_mappings.push_back(evaluate_and_repair(std::move(new_mapping), eval));
	}

	return new_mappings;
}

template <class CostPolicy>
std::vector<MappingView> NSGAIIMapper<CostPolicy>::select(std::vector<std::pair<Mapping, Time>> const& population, size_t parent_population_size) const {

	std::vector<MappingView> parent_selection;
	for (size_t i = 0; i < parent_population_size; ++i) {
		size_t first_idx = rand() % population.size();
		size_t second_idx = rand() % population.size();

		if (population[first_idx].second < population[second_idx].second) {
			parent_selection.push_back(&population[first_idx].first);
		} else {
			parent_selection.push_back(&population[second_idx].first);
		}
	}

	return parent_selection;
}

template <class CostPolicy>
void NSGAIIMapper<CostPolicy>::mutate(std::vector<MappingView>& parent_selection, System const& sys) const {
	std::vector<Task*> tasks = sys.get_task_graph().get_tasks();
	std::vector<Processor*> processors = sys.get_platform().get_processors();
	for (MappingView& parent : parent_selection) {
		for (Task* task : tasks) {
			// Mutation probability of 1/n
			if (rand() % tasks.size() == 0) {
				parent.map(task, processors[rand() % processors.size()]);
			}
		}
	}
}

template <class CostPolicy>
std::pair<Mapping, Time> NSGAIIMapper<CostPolicy>::evaluate_and_repair(Mapping&& mapping, MappingEvaluator const& eval) const {
	std::vector<Task*> const& tasks = eval.get_sys().get_task_graph().get_tasks();
	for (Task* task : tasks) {
		if (!eval.get_sys().is_compatible(task, mapping.get_processor(task))) {
			mapping.map(task, default_proc);
		}
	}

	Processor const* conflicting_proc;
	while (!eval.satisfies_capacity_constraint(mapping, &conflicting_proc)) {
		std::vector<Task*> conflicting_tasks;
		Area total_area = 0;
		for (Task* task : tasks) {
			if (mapping.get_processor(task) == conflicting_proc) {
				total_area += task->get_area_requirement();
				conflicting_tasks.push_back(task);
			}
		}

		while (total_area > conflicting_proc->get_maximum_capacity()) {
			size_t swap_idx = rand() % conflicting_tasks.size();
			total_area -= conflicting_tasks[swap_idx]->get_area_requirement();
			mapping.map(conflicting_tasks[swap_idx], default_proc);
			conflicting_tasks[swap_idx] = conflicting_tasks.back();
			conflicting_tasks.pop_back();
		}
	}

	return std::make_pair(mapping, CostPolicy::compute_cost(mapping, eval));
}

template <class CostPolicy>
std::pair<Mapping, Time> NSGAIIMapper<CostPolicy>::create_valid_random_mapping(MappingEvaluator const& eval) const {
	std::vector<Processor*> processors = eval.get_sys().get_platform().get_processors();

	Mapping mapping;
	for (Task* task : eval.get_sys().get_task_graph().get_tasks()) {
		mapping.map(task, processors[rand() % processors.size()]);
	}

	return evaluate_and_repair(std::move(mapping), eval);
}

template class NSGAIIMapper<FullEvaluation>;
template class NSGAIIMapper<SummedEvaluation>;