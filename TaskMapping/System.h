#pragma once

#include "TaskGraph.h"
#include "Platform.h"

class System {
public:
	virtual Time computation_time_ms(Task*, Processor const*) const = 0;
	virtual Time transaction_time_ms(DataSize const&, Device const*, Device const*) const = 0;
	virtual bool is_compatible(Task*, Device const*) const = 0;

	virtual TaskGraph const& get_task_graph() const = 0;
	virtual Platform const& get_platform() const = 0;
};