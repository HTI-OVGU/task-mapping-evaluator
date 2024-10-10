#pragma once

#include "Mapper.h"

class DeviceBasedMILPMapper : public Mapper {
public:
	Mapping get_task_mapping(System const&) const;
};