#pragma once

#include "Mapper.h"

typedef double Temperature;

class SimulatedAnnealingMapper : public Mapper {
public:
	Mapping get_task_mapping(System const&) const;
protected:
	virtual MappingView iterate(Mapping& curr_mapping, System const& sys) const;
	virtual bool accept(Time const& cost_diff, Time const& initial_cost, Temperature const& temperature) const;
	virtual Temperature get_normalized_final_temperature(System const& sys) const;
	virtual void adjust_temperature(Temperature& temperature) const;
};