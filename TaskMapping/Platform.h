#pragma once

#include "types.h"

#include <cassert>
#include <vector>
#include <string>
#include <unordered_map>

enum class DeviceType { NONE, MEMORY, PROCESSOR };

class Device {
	std::string label;
	DeviceType type;
	bool streaming_allowed;
public:
	Device(std::string const& label, DeviceType const& type, bool streaming_allowed) : label(label), type(type), streaming_allowed(streaming_allowed) {}
    virtual ~Device(){};

	std::string const& get_label() const { return label; }
	bool is_streaming_device() const { return streaming_allowed; }
	virtual DataRate data_movement_rate_MBps() const = 0;
};

class Memory;

class Processor : public Device {
	DataRate serial_processing_rate_MBps = 0;
	DataRate parallel_processing_rate_MBps = 0;
	Area capacity = std::numeric_limits<Area>::infinity();
	Memory* default_memory = nullptr;

public:
	Processor(std::string const& label, bool streaming_allowed) : Device(label, DeviceType::PROCESSOR, streaming_allowed) {}

	void set_processing_rate(DataRate const& serial_rate_MBps) {
		set_processing_rate(serial_rate_MBps, serial_rate_MBps);
	}

	void set_processing_rate(DataRate const& serial_rate_MBps, DataRate const& parallel_rate_MBps) {
		serial_processing_rate_MBps = serial_rate_MBps;
		parallel_processing_rate_MBps = parallel_rate_MBps;
	}

	void set_capacity(Area const& capacity) {
		this->capacity = capacity;
	}

	void set_default_memory(Memory* const memory) {
		default_memory = memory;
	}

	Memory* get_default_memory() const {
		return default_memory;
	}

	bool has_maximum_capacity() const { return capacity < std::numeric_limits<Area>::infinity(); }
	Area const& get_maximum_capacity() const { return capacity; }

	Time processing_time_ms(DataSize const& task_size_MB, Percent const& parallelizability = 0) const {
		assert(/*0 <= parallelizability &&*/ parallelizability <= 100);
		if (serial_processing_rate_MBps <= 0) {
			return std::numeric_limits<Time>::infinity();
		}

		assert(parallel_processing_rate_MBps > 0);
		return ((100 - parallelizability) / serial_processing_rate_MBps + parallelizability / parallel_processing_rate_MBps) * 10 * task_size_MB;
	}

	DataRate data_movement_rate_MBps() const { return parallel_processing_rate_MBps; }
};

class Memory : public Device {
	DataRate data_rate_MBps = 0;
public:
	Memory(std::string const& label, bool streaming_allowed) : Device(label, DeviceType::MEMORY, streaming_allowed) {}

	void set_data_rate(DataRate const& data_rate_MBps) {
		this->data_rate_MBps = data_rate_MBps;
	}

	DataRate data_movement_rate_MBps() const { return data_rate_MBps; }
};

class Platform {
public:
	Platform() = default;
	Platform(Platform&& other) noexcept :
		processors(std::move(other.processors)),
		memories(std::move(other.memories)),
		datarates(std::move(other.datarates))
	{}

	~Platform() {
		for (Processor* p : processors) {
			delete p;
		}
		for (Memory* m : memories) {
			delete m;
		}
	}

	std::vector<Processor*> const& get_processors() const { return processors; }
	std::vector<Memory*> const& get_memories() const { return memories; }

	Processor* create_processor(std::string const& label, bool streaming_allowed = false) {
		processors.push_back(new Processor(label, streaming_allowed));
		return processors.back();
	}

	Memory* create_memory(std::string const& label, bool streaming_allowed = true) {
		memories.push_back(new Memory(label, streaming_allowed));
		return memories.back();
	}

	void set_data_connection(Device const* dev1, Device const* dev2) {
		set_data_connection(dev1, dev2, std::min(dev1->data_movement_rate_MBps(), dev2->data_movement_rate_MBps()));
	}

	void set_data_connection(Device const* dev1, Device const* dev2, DataRate const& rate) {
        set_directed_connection(dev1,dev2,rate);
        set_directed_connection(dev2,dev1,rate);
	}

    void set_directed_connection(Device const* dev1, Device const* dev2, DataRate const& rate) {
        datarates[dev1][dev2] = rate;
    }

	DataRate transfer_rate_MBps(Device const* dev1, Device const* dev2) const {
		if (dev1 == dev2) {
			return std::numeric_limits<DataRate>::infinity();
		}
		if (datarates.contains(dev1)) {
			auto const& second_level = datarates.at(dev1);
			if (second_level.contains(dev2)) {
				return second_level.at(dev2);
			}
		}
		return 0;
	}

private:
	std::vector<Processor*> processors;
	std::vector<Memory*> memories;
    std::unordered_map < Device const*, std::unordered_map<Device const*, DataRate> > datarates;
};