#include "TimeBasedMILPMapper.h"
#include "TopologicalSorting.h"
#include "MILPUtility.h"

double const GUROBI_LARGE_VALUE = 1e4;

Mapping TimeBasedMILPMapper::get_task_mapping(System const& sys) const {
    Mapping mapping;

    std::vector<Task*> const& tasks = sys.get_task_graph().get_tasks();
    std::vector<Edge*> const& edges = sys.get_task_graph().get_edges();
    std::vector<Processor*> const& processors = sys.get_platform().get_processors();
    std::vector<Memory*> const& memories = sys.get_platform().get_memories();

    BFSSorting sorting(sys.get_task_graph(), false);
    std::vector<GraphElement> const& sorted_elements = sorting.get_sorted_elements();

    try {

        // Create an environment
        GRBEnv env = GRBEnv(true);
#ifndef NDEBUG
        env.set("LogFile", streaming_enabled ? "TimeBasedMilpMapperStream.log" : "TimeBasedMilpMapper.log");
#endif
        env.set("OutputFlag", "0");
        env.set(GRB_DoubleParam_TimeLimit, TIMEOUT_VAL);
        env.start();

        // Create an empty model
        GRBModel model = GRBModel(env);

        TaskDeviceMap tproc, tmin, tmout;
        setup_task_device_variables(sys, model, tproc, tmin, tmout);
        setup_capacity_constraints(sys, model, tproc);

        std::unordered_map<Task*, GRBVar> y0, y1;
        for (Task* task : tasks) {
            std::string task_label = task->get_label();

            y0[task] = model.addVar(0.0, std::numeric_limits<float>::infinity(), 0.0, GRB_CONTINUOUS, "Y0_" + task_label);
            y1[task] = model.addVar(0.0, std::numeric_limits<float>::infinity(), 0.0, GRB_CONTINUOUS, "Y1_" + task_label);
        }

        GRBVar z = model.addVar(0.0, std::numeric_limits<float>::infinity(), 0.0, GRB_CONTINUOUS, "Z");
        model.setObjective(GRBLinExpr(z), GRB_MINIMIZE);

        for (Task* task : tasks) {
            GRBQuadExpr expr = 0;
            GRBQuadExpr expr_comp = 0;
            GRBQuadExpr expr_min = 0;
            GRBQuadExpr expr_mout = 0;

            for (Processor* proc : processors) {

                GRBQuadExpr *ec, *ei, *eo;
                if (streaming_enabled && proc->is_streaming_device()) {
                    ec = &expr_comp;
                    ei = &expr_min;
                    eo = &expr_mout;
                }
                else {
                    ec = ei = eo = &expr;
                }
                
                add_scaled_product_to_expr(*ec, sys.computation_time_ms(task, proc), tproc[task][proc], model);

                for (Memory* mem : memories) {
                    add_scaled_product_to_expr(*ei, sys.transaction_time_ms(task->get_input_size(), mem, proc), tmin[task][mem] * tproc[task][proc], model);
                    add_scaled_product_to_expr(*eo, sys.transaction_time_ms(task->get_output_size(), proc, mem), tproc[task][proc] * tmout[task][mem], model);
                }
            }

            model.addConstr(y1[task] <= z);
            model.addQConstr(y0[task] + expr <= y1[task]);

            if (streaming_enabled) {
                model.addQConstr(y0[task] + expr_comp <= y1[task]);
                model.addQConstr(y0[task] + expr_min <= y1[task]);
                model.addQConstr(y0[task] + expr_mout <= y1[task]);
            }
        }

        std::unordered_map<Task*, std::unordered_map<Processor*, std::unordered_map<Memory*, GRBVar> > > in_comb, out_comb;
        if (streaming_enabled) {
            for (Processor* streaming_proc : processors) {
                if (streaming_proc->is_streaming_device()) {
                    for (Task* task : tasks) {
                        std::string const& task_label = task->get_label();
                        std::string const& proc_label = streaming_proc->get_label();
                        for (Memory* mem : memories) {
                            std::string const& mem_label = mem->get_label();
                            in_comb[task][streaming_proc][mem] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY, "IC_"+task_label+"_"+proc_label+"_"+mem_label);
                            out_comb[task][streaming_proc][mem] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY, "OC_" + task_label + "_" + proc_label + "_" + mem_label);

                            model.addQConstr(tproc[task][streaming_proc] * tmin[task][mem] == in_comb[task][streaming_proc][mem]);
                            model.addQConstr(tproc[task][streaming_proc] * tmout[task][mem] == out_comb[task][streaming_proc][mem]);
                        }
                    }
                }
            }
        }

        std::unordered_map<Edge*, GRBVar > y1s;
        for (Edge* edge : edges) {
            GRBQuadExpr expr = 0;
            GRBQuadExpr streaming_expr = 0;
            Task* const t_out = edge->get_src();
            Task* const t_in = edge->get_snk();

            std::string const& tout_label = t_out->get_label();
            std::string const& tin_label = t_in->get_label();

            for (Memory* mem_out : memories) {
                for (Memory* mem_in : memories) {
                    Time const transaction_time = sys.transaction_time_ms(t_out->get_output_size(), mem_out, mem_in);
                    if (streaming_enabled) {
                        for (Processor* streaming_proc : processors) {
                            if (streaming_proc->is_streaming_device() /* && mem_out->is_streaming_device() && mem_in->is_streaming_device()*/) {
                                add_scaled_product_to_expr(streaming_expr, transaction_time, out_comb[t_out][streaming_proc][mem_out] * in_comb[t_in][streaming_proc][mem_in], model);
                            }
                        }
                    }
                    add_scaled_product_to_expr(expr, transaction_time, tmout[t_out][mem_out] * tmin[t_in][mem_in], model);
                }
            }

            if (streaming_enabled) {
                y1s[edge] = model.addVar(0.0, std::numeric_limits<float>::infinity(), 0.0, GRB_CONTINUOUS, "Y1S_"+tout_label+"_"+tin_label);

                GRBQuadExpr expr_both_streaming = 0;
                for (Processor* streaming_proc : processors) {
                    if (streaming_proc->is_streaming_device()) {
                        expr_both_streaming += tproc[t_out][streaming_proc] * tproc[t_in][streaming_proc];
                    }
                }
                model.addQConstr(y1[t_out] - GUROBI_LARGE_VALUE * expr_both_streaming <= y1s[edge], "CEDGEY1S_" + tout_label + "_" + tin_label);
            }

            if (streaming_enabled) {
                model.addConstr(y0[t_out] <= y0[t_in], "CEDGEY00_" + tout_label + "_" + tin_label);
                model.addConstr(y1[t_out] <= y1[t_in], "CEDGEY11_" + tout_label + "_" + tin_label);

                model.addQConstr(y1s[edge] + expr - streaming_expr <= y0[t_in], "CEDGEY1S0_" + tout_label + "_" + tin_label);
                model.addQConstr(y0[t_out] + streaming_expr <= y1[t_in], "CEDGEY01_" + tout_label + "_" + tin_label);
            }
            else {
                model.addQConstr(y1[t_out] + expr <= y0[t_in], "CEDGEY10_" + tout_label + "_" + tin_label);
            }            
        }

        for (size_t i = 0; i < sorted_elements.size(); ++i) {
            Task* ti = sorted_elements[i].get_task();
            for (size_t j = i + 1; j < sorted_elements.size(); ++j) {
                Task* tj = sorted_elements[j].get_task();

                std::string const& ti_label = ti->get_label();
                std::string const& tj_label = tj->get_label();

                GRBQuadExpr proc_expr = 0;
                for (Processor* proc : processors) {
                    if (!streaming_enabled || !proc->is_streaming_device()) {
                        proc_expr += tproc[ti][proc] * tproc[tj][proc];
                    }
                }
                model.addQConstr(GUROBI_LARGE_VALUE* proc_expr - GUROBI_LARGE_VALUE <= y0[tj] - y1[ti], "CTOPP_"+ti_label+"_"+tj_label);

                GRBQuadExpr mem_ii_expr = 0;
                GRBQuadExpr mem_io_expr = 0;
                GRBQuadExpr mem_oi_expr = 0;
                GRBQuadExpr mem_oo_expr = 0;
                for (Memory* mem : memories) {
                    if (streaming_enabled) {
                        for (Processor* streaming_proc : processors) {
                            if (streaming_proc->is_streaming_device()) {
                                mem_ii_expr -= in_comb[ti][streaming_proc][mem] * in_comb[tj][streaming_proc][mem];
                                mem_io_expr -= in_comb[ti][streaming_proc][mem] * out_comb[tj][streaming_proc][mem];
                                mem_oi_expr -= out_comb[ti][streaming_proc][mem] * in_comb[tj][streaming_proc][mem];
                                mem_oo_expr -= out_comb[ti][streaming_proc][mem] * out_comb[tj][streaming_proc][mem];
                            }
                        }
                    }

                    mem_ii_expr += tmin[ti][mem] * tmin[tj][mem];
                    mem_io_expr += tmin[ti][mem] * tmout[tj][mem];
                    mem_oi_expr += tmout[ti][mem] * tmin[tj][mem];
                    mem_oo_expr += tmout[ti][mem] * tmout[tj][mem];
                }
                model.addQConstr(GUROBI_LARGE_VALUE * mem_ii_expr - GUROBI_LARGE_VALUE <= y0[tj] - y1[ti], "CTOPII_" + ti_label + "_" + tj_label);
                model.addQConstr(GUROBI_LARGE_VALUE * mem_io_expr - GUROBI_LARGE_VALUE <= y0[tj] - y1[ti], "CTOPIO_" + ti_label + "_" + tj_label);
                model.addQConstr(GUROBI_LARGE_VALUE * mem_oi_expr - GUROBI_LARGE_VALUE <= y0[tj] - y1[ti], "CTOPOI_" + ti_label + "_" + tj_label);
                model.addQConstr(GUROBI_LARGE_VALUE * mem_oo_expr - GUROBI_LARGE_VALUE <= y0[tj] - y1[ti], "CTOPOO_" + ti_label + "_" + tj_label);
            }
        }

        model.optimize();

#ifndef NDEBUG
        print_debug_data(model, streaming_enabled ? "TimeBasedStreamingDbg.lp" : "TimeBasedDbg.lp");
#endif

        if (model.get(GRB_IntAttr_Status) == GRB_TIME_LIMIT) {
            return mapping;
        }

        extract_mapping_from_variables(mapping, sys, tproc, tmin, tmout);
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