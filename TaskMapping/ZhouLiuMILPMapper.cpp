#include "ZhouLiuMILPMapper.h"
#include "MILPUtility.h"

Mapping ZhouLiuMILPMapper::get_task_mapping(System const& sys) const {
    Mapping mapping;
    std::vector<Task*> const& tasks = sys.get_task_graph().get_tasks();
    std::vector<Edge*> const& edges = sys.get_task_graph().get_edges();
    std::vector<Processor*> const& processors = sys.get_platform().get_processors();

    try {

        // Create an environment
        GRBEnv env = GRBEnv(true);
        env.set("OutputFlag", "0");
        env.set(GRB_DoubleParam_TimeLimit, TIMEOUT_VAL);
#ifndef NDEBUG
        env.set("LogFile", "ZhouLiuMILPMapper.log");
#endif
        env.start();

        // Create an empty model
        GRBModel model = GRBModel(env);

        GRBVar z = model.addVar(0.0, std::numeric_limits<double>::infinity(), 0.0, GRB_CONTINUOUS, "Z");
        model.setObjective(GRBLinExpr(z), GRB_MINIMIZE);

        std::vector<TaskDeviceMap> vars;
        for (size_t i = 0; i < tasks.size(); ++i) {
            vars.push_back(TaskDeviceMap());
            for (Task* task : tasks) {
                std::string const task_label = task->get_label();
                for (Processor* proc : processors) {
                    vars[i][task][proc] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY, "P_" + task_label + "_" + proc->get_label() + "_" + std::to_string(i));
                    if (!sys.is_compatible(task, proc)) {
                        model.addConstr(0 == vars[i][task][proc]);
                    }
                }
            }
        }

        std::unordered_map<Task*, GRBVar> times;
        for (Task* task : tasks) {
            times[task] = model.addVar(0.0, std::numeric_limits<double>::infinity(), 0.0, GRB_CONTINUOUS, "S_" + task->get_label());
        }


        for (Task* task : tasks) {
            GRBLinExpr constr1 = 0;
            for (TaskDeviceMap& timestep : vars) {
                for (Processor* proc : processors) {
                    constr1 += timestep[task][proc];
                }
            }
            model.addConstr(constr1 == 1);
        }

        for (Processor* proc : processors) {
            GRBLinExpr constr2 = 0;
            for (Task* task : tasks) {
                constr2 += vars[0][task][proc];
            }
            model.addConstr(constr2 <= 1);

            for (size_t i = 1; i < tasks.size(); ++i) {
                GRBLinExpr constr3 = 0;
                for (Task* task : tasks) {
                    constr3 += vars[i-1][task][proc] - vars[i][task][proc];
                }
                model.addConstr(constr3 >= 0);
            }
        }

        for (Edge* e : edges) {
            GRBQuadExpr constr4 = times[e->get_src()];
            for (size_t i = 0; i < tasks.size(); ++i) {
                for (Processor* proc : processors) {
                    constr4 += sys.computation_time_ms(e->get_src(), proc) * vars[i][e->get_src()][proc];

                    for (size_t j = 0; j < tasks.size(); ++j) {
                        for (Processor* next_proc : processors) {
                            Time ttime = sys.transaction_time_ms(e->get_src()->get_output_size(), proc->get_default_memory(), next_proc->get_default_memory());
                            if (ttime == std::numeric_limits<Time>::infinity()) {
                                model.addQConstr(vars[i][e->get_src()][proc] * vars[j][e->get_snk()][next_proc] == 0);
                            }
                            else {
                                constr4 += sys.transaction_time_ms(e->get_src()->get_output_size(), proc->get_default_memory(), next_proc->get_default_memory()) * vars[i][e->get_src()][proc] * vars[j][e->get_snk()][next_proc];
                            }
                        }
                    }
                }
            }
            model.addQConstr(times[e->get_snk()] >= constr4);
        }

        for (size_t i = 1; i < tasks.size(); ++i) {
            for (Processor* proc : processors) {
                for (Task* task : tasks) {
                    for (Task* other_task : tasks) {
                        GRBLinExpr constr5 = vars[i][other_task][proc];
                        for (size_t j = i + 1; j < sys.get_task_graph().get_tasks().size(); ++j) {
                            constr5 += vars[j][task][proc];
                        }
                        constr5 = times[other_task] + sys.computation_time_ms(other_task, proc) - 1000 * (2 - constr5);
                        model.addConstr(times[task] >= constr5);
                    }
                }
            }
        }

        for (Task* task : tasks) {
            GRBLinExpr constr6 = times[task];
            for (TaskDeviceMap& timestep : vars) {
                for (Processor* proc : processors) {
                    constr6 += sys.computation_time_ms(task, proc) * timestep[task][proc];
                }
            }
            model.addConstr(z >= constr6);
            model.addConstr(times[task] >= 0);
        }

        for (Processor* proc : processors) {
            if (proc->has_maximum_capacity()) {
                GRBLinExpr expr = 0;
                for (TaskDeviceMap& timestep : vars) {
                    for (Task* task : sys.get_task_graph().get_tasks()) {
                        expr += task->get_area_requirement() * timestep[task][proc];
                    }
                }
                model.addConstr(proc->get_maximum_capacity() >= expr, "CAPACITY_" + proc->get_label());
            }
        }

        model.optimize();

#ifndef NDEBUG
        print_debug_data(model, "ZhouLiuDbg.lp");
#endif
        if (model.get(GRB_IntAttr_Status) == GRB_TIME_LIMIT) {
            return mapping;
        }

        if (model.get(GRB_IntAttr_Status) == GRB_OPTIMAL) {
            for (Task* task : tasks) {
                Processor* mapped_proc = nullptr;

                for (TaskDeviceMap& timestep : vars) {
                    for (Processor* proc : processors) {
                        if (timestep.at(task).at(proc).get(GRB_DoubleAttr_X) > 0.9) {
                            mapped_proc = proc;
                            break;
                        }
                    }
                    if (mapped_proc) break;
                }
                if (mapped_proc) mapping.map(task, mapped_proc, mapped_proc->get_default_memory(), mapped_proc->get_default_memory());
            }
        }
    }
    catch (GRBException e) {
        std::cout << "Error code = " << e.getErrorCode() << std::endl;
        std::cout << e.getMessage() << std::endl;
    }
    catch (...) {
        std::cout << "Exception during optimization" << std::endl;
    }

    return mapping;
}