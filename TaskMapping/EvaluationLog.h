#pragma once
#include "TaskGraph.h"
#include <unordered_map>

struct TimeRange {
	Time start_time_ms;
	Time end_time_ms;
};

class EvaluationLog {
	std::unordered_map<Task*, TimeRange> computation_times;
	std::unordered_map<Edge*, TimeRange> transfer_times;

public:

	void log(Task* task, Time const& start_time_ms, Time const& end_time_ms) {
		computation_times[task] = { start_time_ms, end_time_ms };
	}

	void log(Edge* edge, Time const& start_time_ms, Time const& end_time_ms) {
		transfer_times[edge] = { start_time_ms, end_time_ms };
	}

	bool contains(Task* task) const { return computation_times.contains(task); }
	bool contains(Edge* edge) const { return transfer_times.contains(edge); }

	Time start_time_ms(Task* task) const { return computation_times.contains(task) ? computation_times.at(task).start_time_ms : -1; }
	Time start_time_ms(Edge* edge) const { return transfer_times.contains(edge) ? transfer_times.at(edge).start_time_ms : -1; }
	Time end_time_ms(Task* task) const { return computation_times.contains(task) ? computation_times.at(task).end_time_ms : -1; }
	Time end_time_ms(Edge* edge) const { return transfer_times.contains(edge) ? transfer_times.at(edge).end_time_ms : -1; }
};