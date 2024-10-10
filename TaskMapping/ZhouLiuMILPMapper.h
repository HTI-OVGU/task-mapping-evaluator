#pragma once

#include "Mapper.h"

class ZhouLiuMILPMapper : public Mapper {
	Mapping get_task_mapping(System const&) const;
};