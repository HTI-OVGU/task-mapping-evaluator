#pragma once

#include "Mapper.h"

class TimeBasedMILPMapper : public Mapper {
	bool streaming_enabled;
public:
	TimeBasedMILPMapper(bool enable_streaming = false) : Mapper(), streaming_enabled(enable_streaming) {}
	Mapping get_task_mapping(System const&) const;
};