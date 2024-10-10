cmake_minimum_required(VERSION 3.25)
project(TaskMapping)

enable_language(CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-O3 -Wall -Wextra")

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

include_directories(.)

add_library(TaskMappingLib
        ComputationBasedSystem.h
        DecompositionMapper.h
        DecompositionMapperPolicies.h
        DeviceBasedMILPMapper.cpp
        DeviceBasedMILPMapper.h
        DrawGraph.cpp
        DrawGraph.h
        Evaluation.h
        EvaluationLog.h
        GraphExport.h
        GreedyMapper.h
        GUID.cpp
        GUID.h
        HEFTMapper.h
        Mapper.h
        Mapping.h
        MappingUtility.h
        MILPUtility.cpp
        MILPUtility.h
		NSGAIIMapper.cpp
		NSGAIIMapper.h
        PathBasedMapper.h
        PEFTMapper.h
        Platform.h
        PlatformGenerator.cpp
        PlatformGenerator.h
        ResultHandling.h
        run_mappings.h
        SafeBoostHeaders.h
        SeriesParallelDecomposition.h
        SeriesParallelDecompositionMapper.h
		SimulatedAnnealingMapper.cpp
		SimulatedAnnealingMapper.h
        SingleNodeDecompositionMapper.h
        System.h
        TaskGraph.cpp
        TaskGraph.h
        TaskGraphGenerator.h
        TaskGraphReader.h
        TaskMapperWithSchedule.h
        tests.h
        TimeBasedMILPMapper.cpp
        TimeBasedMILPMapper.h
        TopologicalSorting.h
        types.h
        ZhouLiuMILPMapper.cpp
        ZhouLiuMILPMapper.h
)

add_executable(TaskMapping main.cpp)
target_link_libraries(TaskMapping TaskMappingLib)

find_package(GUROBI REQUIRED)
target_include_directories(TaskMappingLib PUBLIC ${GUROBI_INCLUDE_DIRS})

set(GUROBI_CXX_LIBRARY "$ENV{GUROBI_HOME}/lib/libgurobi_c++.a") #Should in theory be automatically found
set(GUROBI_CXX_DEBUG_LIBRARY "$ENV{GUROBI_HOME}/lib/libgurobi_c++.a") #Should in theory be automatically found
target_link_libraries(TaskMappingLib optimized ${GUROBI_CXX_LIBRARY}
        debug ${GUROBI_CXX_DEBUG_LIBRARY})
target_link_libraries(TaskMappingLib ${GUROBI_LIBRARY})

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    include(FeatureSummary)
    feature_summary(WHAT ALL)
endif()

#target_include_directories(TaskMapping PRIVATE /opt/gurobi912/linux64/include/ /usr/include/)
#target_link_directories(TaskMapping PRIVATE /opt/gurobi912/linux64/lib/)
#target_link_libraries(TaskMapping gurobi91 gurobi_c++ m)