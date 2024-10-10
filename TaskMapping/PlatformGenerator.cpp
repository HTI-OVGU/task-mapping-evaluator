#include "PlatformGenerator.h"

#ifdef _WIN32
#pragma warning (disable : 26451) // Disable arithmetic overflow warning
#endif

const unsigned GLOBAL_WORD_LENGTH = 4; // Byte

const unsigned CPU_CLOCK_RATE = 2900; // MHz
const unsigned CPU_DATA_PARALLELISM = 1;//8; // Words
const unsigned CPU_CORE_NUMBER = 16;

const unsigned MAIN_RAM_TRANSFER_RATE = 2667; // MHz
const unsigned MAIN_RAM_WIDTH = 8; // Byte
const unsigned MAIN_RAM_CHANNELS = 2; // ? 8;

const unsigned GPU_CLOCK_RATE = 1471; // ~2900/4; // MHz
const unsigned GPU_CORE_NUMBER = 3584;
const unsigned GPU_SIMD_WIDTH  = 32; // Words / SIMD
const unsigned GPU_SIMD_PER_CU = 4; // SIMD / CU
const unsigned GPU_DATA_PARALLELISM = 1;//GPU_SIMD_WIDTH * GPU_SIMD_PER_CU;
const double GPU_PENALTY = 12./5;

const unsigned GPU_RAM_TRANSFER_RATE = 800; // MHz
const unsigned GPU_RAM_WIDTH = 256; // Byte
const unsigned GPU_RAM_CHANNELS = 1;

/*const unsigned CPU_CLOCK_RATE = 2400; // MHz
const unsigned CPU_DATA_PARALLELISM = 8; // Words
const unsigned CPU_CORE_NUMBER = 8;

const unsigned MAIN_RAM_TRANSFER_RATE = 2666; // MHz
const unsigned MAIN_RAM_WIDTH = 8; // Byte
const unsigned MAIN_RAM_CHANNELS = 8;

const unsigned GPU_CLOCK_RATE = 1590; // MHz
const unsigned GPU_CORE_NUMBER = 56;
const unsigned GPU_SIMD_WIDTH = 16; // Words / SIMD
const unsigned GPU_SIMD_PER_CU = 4; // SIMD / CU
const unsigned GPU_DATA_PARALLELISM = GPU_SIMD_WIDTH * GPU_SIMD_PER_CU;

const unsigned GPU_RAM_TRANSFER_RATE = 1600; // MHz
const unsigned GPU_RAM_WIDTH = 256; // Byte
const unsigned GPU_RAM_CHANNELS = 1;*/

const int FPGA_STREAMING_RATE = 400; // MHz
const unsigned FPGA_CAPACITY = 128; // Abstract capacity units

const unsigned FPGA_RAM_TRANSFER_RATE = 1600; // MHz
const unsigned FPGA_RAM_WIDTH = 8; // Byte
const double FPGA_RAM_CHANNELS = 1.5;

Platform create_platform(int nbr_fpgas) {
	Platform p;
	Processor* CPU = p.create_processor("CPU");
	CPU->set_processing_rate(GLOBAL_WORD_LENGTH * CPU_CLOCK_RATE, GLOBAL_WORD_LENGTH * CPU_CLOCK_RATE * CPU_CORE_NUMBER * CPU_DATA_PARALLELISM);

	Memory* MAIN_RAM = p.create_memory("Main_RAM");
	MAIN_RAM->set_data_rate(MAIN_RAM_TRANSFER_RATE * MAIN_RAM_WIDTH * MAIN_RAM_CHANNELS);
	CPU->set_default_memory(MAIN_RAM);

	Processor* GPU = p.create_processor("GPU");
	GPU->set_processing_rate((GLOBAL_WORD_LENGTH * GPU_CLOCK_RATE) / GPU_PENALTY, (GLOBAL_WORD_LENGTH * GPU_CLOCK_RATE * GPU_CORE_NUMBER * GPU_DATA_PARALLELISM) / GPU_PENALTY);

	Memory* GPU_RAM = p.create_memory("GPU_RAM");
	GPU_RAM->set_data_rate(GPU_RAM_TRANSFER_RATE * GPU_RAM_WIDTH * GPU_RAM_CHANNELS);
	GPU->set_default_memory(GPU_RAM);

	p.set_data_connection(CPU, MAIN_RAM);
	p.set_data_connection(GPU, GPU_RAM);
	p.set_data_connection(MAIN_RAM, GPU_RAM /*, 8000 * 0.985 * 16 / 8*/);


    /*Processor* CPU2 = p.create_processor("SCPU");
    CPU->set_processing_rate(GLOBAL_WORD_LENGTH * CPU_CLOCK_RATE, GLOBAL_WORD_LENGTH * CPU_CLOCK_RATE);
    p.set_data_connection(CPU2, MAIN_RAM);*/

	//p.set_data_connection(GPU, MAIN_RAM, std::min(GPU_RAM->data_movement_rate_MBps(), p.transfer_rate_MBps(MAIN_RAM, GPU_RAM)));
	//p.set_data_connection(CPU, GPU_RAM, std::min(MAIN_RAM->data_movement_rate_MBps(), p.transfer_rate_MBps(MAIN_RAM, GPU_RAM)));

	for (int i = 0; i < nbr_fpgas; ++i) {
		std::string id = nbr_fpgas > 1 ? std::to_string(i) : "";

		Processor* FPGA = p.create_processor("FPGA"+id, true);
		FPGA->set_processing_rate(GLOBAL_WORD_LENGTH * FPGA_STREAMING_RATE);
		FPGA->set_capacity(FPGA_CAPACITY);

		Memory* FPGA_RAM = p.create_memory("FPGA_RAM" + id);
		FPGA_RAM->set_data_rate(FPGA_RAM_TRANSFER_RATE * FPGA_RAM_WIDTH * FPGA_RAM_CHANNELS);
		FPGA->set_default_memory(FPGA_RAM);

		p.set_data_connection(FPGA, FPGA_RAM, FPGA_STREAMING_RATE * 32 * 1 * 7 / 8);
		p.set_data_connection(MAIN_RAM, FPGA_RAM, FPGA_STREAMING_RATE * 64 * 1 / 8);
	}

	return p;
}
