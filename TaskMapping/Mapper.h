#pragma once

#include "System.h"
#include "Mapping.h"

class Mapper {
public:
	virtual Mapping get_task_mapping(System const&) const = 0;
};