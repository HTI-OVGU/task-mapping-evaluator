#pragma once

#include "Mapper.h"
#include "Evaluation.h"

class FullEvaluation {
public:
	static Time compute_cost(Mapping const& mapping, MappingEvaluator const& eval) {
		return eval.compute_cost(mapping);
	}
};

class SummedEvaluation {
public:
	static Time compute_cost(Mapping const& mapping, MappingEvaluator const& eval) {
		std::unordered_map<Processor const*, Time> summed_time;
		for (Processor* proc : eval.get_sys().get_platform().get_processors()) {
			summed_time[proc] = 0;
		}
		for (Task* task : eval.get_sys().get_task_graph().get_tasks()) {
			Processor const* const proc = mapping.get_processor(task);
			summed_time[proc] += eval.get_sys().computation_time_ms(task, proc);
		}
		for (Edge* edge : eval.get_sys().get_task_graph().get_edges()) {
			Processor const* const in_proc = mapping.get_processor(edge->get_src());
			Processor const* const out_proc = mapping.get_processor(edge->get_snk());

			Time const transfer_time = eval.get_sys().transaction_time_ms(edge->get_src()->get_output_size(), in_proc, out_proc);
			summed_time[in_proc] += transfer_time;
			summed_time[out_proc] += transfer_time;
		}
		Time max = 0;
		for (Processor* proc : eval.get_sys().get_platform().get_processors()) {
			if (max < summed_time[proc]) {
				max = summed_time[proc];
			}
		}

		return max;
	}
};

template <class CostPolicy = FullEvaluation> class NSGAIIMapper : public Mapper {
	mutable Processor* default_proc = nullptr;
	size_t const GENERATIONS;
public:
	NSGAIIMapper(size_t generations = 500) : GENERATIONS(generations) {};
	Mapping get_task_mapping(System const&) const;
protected:
	void init(System const&) const;
	std::pair<Mapping, Time> evaluate_and_repair(Mapping&& mapping, MappingEvaluator const& eval) const;
	std::pair<Mapping, Time> create_valid_random_mapping(MappingEvaluator const& eval) const;
	std::vector<MappingView> select(std::vector<std::pair<Mapping, Time>> const& population, size_t parent_population_size) const;
	void mutate(std::vector<MappingView>& parent_selection, System const& sys) const;
	std::vector<std::pair<Mapping, Time>> crossover(std::vector<MappingView> const& parent_selection, std::vector<GraphElement> const& sorted_tasks, MappingEvaluator const& eval) const;
};
