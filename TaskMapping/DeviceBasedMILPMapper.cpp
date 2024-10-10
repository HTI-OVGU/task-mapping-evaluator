#include "DeviceBasedMILPMapper.h"
#include "MILPUtility.h"

Mapping DeviceBasedMILPMapper::get_task_mapping(System const& sys) const {
    Mapping mapping;
    std::vector<Task*> const& tasks = sys.get_task_graph().get_tasks();
    std::vector<Edge*> const& edges = sys.get_task_graph().get_edges();
    std::vector<Processor*> const& processors = sys.get_platform().get_processors();
    std::vector<Memory*> const& memories = sys.get_platform().get_memories();

    try {

        // Create an environment
        GRBEnv env = GRBEnv(true);
        env.set("OutputFlag", "0");
        env.set(GRB_DoubleParam_TimeLimit, TIMEOUT_VAL);
#ifndef NDEBUG
        env.set("LogFile", "DeviceBasedMILPMapper.log");
#endif
        env.start();

        // Create an empty model
        GRBModel model = GRBModel(env);

        TaskDeviceMap tproc, tmin, tmout;
        setup_task_device_variables(sys, model, tproc, tmin, tmout);
        setup_capacity_constraints(sys, model, tproc);

        GRBVar z = model.addVar(0.0, std::numeric_limits<double>::infinity(), 0.0, GRB_CONTINUOUS, "Z");
        model.setObjective(GRBLinExpr(z), GRB_MINIMIZE);

        for (Processor* proc : processors) {
            GRBQuadExpr expr = 0;
            for (Task* task : tasks) {
                add_scaled_product_to_expr(expr, sys.computation_time_ms(task, proc), tproc[task][proc], model);

                for (Memory* dev_q : memories) {
                    add_scaled_product_to_expr(expr, sys.transaction_time_ms(task->get_input_size(), dev_q, proc), tmin[task][dev_q] * tproc[task][proc], model);
                    add_scaled_product_to_expr(expr, sys.transaction_time_ms(task->get_output_size(), proc, dev_q), tproc[task][proc] * tmout[task][dev_q], model);
                }
            }

            model.addQConstr(expr <= z);
        }

        for (Memory* dev_p : memories) {
            GRBQuadExpr expr = 0;
            for (Task* task : tasks) {
                for (Processor* proc : processors) {
                    add_scaled_product_to_expr(expr, sys.transaction_time_ms(task->get_output_size(), proc, dev_p), tproc[task][proc] * tmout[task][dev_p], model);
                    add_scaled_product_to_expr(expr, sys.transaction_time_ms(task->get_input_size(), dev_p, proc), tmin[task][dev_p] * tproc[task][proc], model);
                }
            }
            for (Edge* edge : edges) {
                for (Memory* dev_q : memories) {
                    add_scaled_product_to_expr(expr, sys.transaction_time_ms(edge->get_src()->get_output_size(), dev_q, dev_p), tmout[edge->get_src()][dev_q] * tmin[edge->get_snk()][dev_p], model);
                    add_scaled_product_to_expr(expr, sys.transaction_time_ms(edge->get_src()->get_output_size(), dev_p, dev_q), tmout[edge->get_src()][dev_p] * tmin[edge->get_snk()][dev_q], model);
                }
            }
            model.addQConstr(expr <= z);
        }

        model.optimize();

#ifndef NDEBUG
        print_debug_data(model, "DeviceBasedDbg.lp");
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