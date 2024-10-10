#pragma once

#include "Platform.h"

class DevicePair {
	Processor const* proc = nullptr;
	Memory const* mem = nullptr;

public:
	DevicePair() {}

	DevicePair(std::string const& proc_label, std::string const& mem_label, Platform const& platform) {
		for (Processor* proc : platform.get_processors()) {
			if (proc->get_label() == proc_label) {
				this->proc = proc;
				break;
			}
		}

		for (Memory* mem : platform.get_memories()) {
			if (mem->get_label() == mem_label) {
				this->mem = mem;
				break;
			}
		}
	}

	DevicePair(Processor* proc, Memory* mem) : proc(proc), mem(mem)
	{}

	bool valid() const {
		return mem && proc;
	}

	Processor const* get_proc() const {
		return proc;
	}

	Memory const* get_mem() const {
		return mem;
	}
};

std::vector<DevicePair> device_pairs_from_platform(Platform const& platform) {
	std::vector<DevicePair> device_pairs;

	if (platform.get_memories().size() > 0) {
		std::string const main_ram_label = "Main_RAM";
		Memory* main_ram = nullptr;
		for (Memory* mem : platform.get_memories()) {
			if (mem->get_label() == main_ram_label) {
				main_ram = mem;
				break;
			}
		}

		if (!main_ram) {
			main_ram = platform.get_memories()[0];
		}

		for (Processor* proc : platform.get_processors()) {
			Memory* proc_mem = proc->get_default_memory();
			device_pairs.push_back({ proc, proc_mem ? proc_mem : main_ram });
		}
	}

	return device_pairs;
}