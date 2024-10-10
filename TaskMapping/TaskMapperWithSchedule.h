#pragma once

#include "Mapper.h"
#include <queue>

class TaskMapperWithSchedule : public Mapper {
protected:
	mutable std::priority_queue<std::pair<Time, Task*>> task_schedule;
public:
	virtual std::priority_queue<std::pair<Time, Task*>> const& get_task_schedule() const {
		return task_schedule;
	}
};