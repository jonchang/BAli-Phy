/*
  Copyright (C) 2004-2010 Benjamin Redelings

  This file is part of BAli-Phy.

  BAli-Phy is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2, or (at your option) any later
  version.

  BAli-Phy is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
  for more details.

  You should have received a copy of the GNU General Public License
  along with BAli-Phy; see the file COPYING.  If not see
  <http://www.gnu.org/licenses/>.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_FENV_H
extern "C" {
#include "fenv.h"
}
#endif

#include "util/time.H"

#ifdef HAVE_MPI
#include <mpi.h>
#include <boost/mpi.hpp>
namespace mpi = boost::mpi;
#endif

#include <cmath>
#include <ctime>
#include <iostream>
#include <sstream>
#include <new>
#include <map>

#include <boost/program_options.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/chrono.hpp>

#include "substitution/substitution.H"
#include "util/myexception.H"
#include "util/rng.H"
#include "util/mapping.H"
#include "util/io.H"
#include "util/string/split.H"
#include "util/log-level.H"
#include "util/set.H"
#include "models/parameters.H"
#include "models/rules.H"
#include "models/setup.H"
#include "mcmc/mcmc.H"
#include "mcmc/logger.H"
#include "math/pow2.H"
#include "version.H"
#include "mcmc/setup.H"
#include "computation/parser/desugar.H"
#include "computation/module.H"
#include "computation/program.H"

#include "A-T-model.H"
#include "files.H"
#include "paths.H"
#include "loggers.H"
#include "io.H"
#include "system.H"
#include "cmd_line.H"
#include "computation/expression/expression.H" // for parse_object( )
#include "computation/loader.H"

namespace fs = boost::filesystem;
namespace chrono = boost::chrono;

namespace po = boost::program_options;
using po::variables_map;

using std::cout;
using std::cerr;
using std::clog;
using std::endl;
using std::ifstream;
using std::ofstream;
using std::ostream;
using std::string;
using std::vector;
using std::map;
using std::set;
using std::optional;

using std::shared_ptr;

/* 
 * 1. Add a PRANK-like initial algorithm.
 * 2. Add some kind of constraint.
 * 3. Improve the method for proposing new SPR attachment sites.
 *  3a. Can we walk along the tree making characters present?
 */

void report_mem() {
#if !defined(_MSC_VER) && !defined(__MINGW32__)
/*
  struct rusage usage;
  std::cerr<<getrusage(RUSAGE_SELF,&usage);
  std::cerr<<"Maximum resident set size: "<<usage.ru_maxrss<<"\n";
  std::cerr<<"Integral shared memory size: "<<usage.ru_ixrss<<"\n";
  std::cerr<<"Integral unshared data size: "<<usage.ru_idrss<<"\n";
  std::cerr<<"Integral unshared stack size: "<<usage.ru_isrss<<"\n";
  std::cerr<<"Number of swaps: "<<usage.ru_nswap<<"\n";
  std::cerr.flush();
*/
  int pid = getpid();
  std::ostringstream cmd;
  cmd<<"cat /proc/"<<pid<<"/status | grep Vm 1>&2";
  system(cmd.str().c_str());
  std::cerr.flush();
#endif
}

#ifdef DEBUG_MEMORY
void * operator new(size_t sz)
    printf("new called, sz = %d\n",sz);
    return malloc(sz); 
}

void operator delete(void * p)
    printf("delete called, content = %d\n",(*(int*)p));
    free(p); 
}
#endif

/// Initialize the default random number generator and return the seed
unsigned long init_rng_and_get_seed(const variables_map& args)
{
    unsigned long seed = 0;
    if (args.count("seed")) {
        seed = args["seed"].as<unsigned long>();
        myrand_init(seed);
    }
    else
        seed = myrand_init();

    return seed;
}

chrono::system_clock::time_point start_time = chrono::system_clock::now();

string ctime(const chrono::system_clock::time_point& t)
{
    time_t t2 = chrono::system_clock::to_time_t(t);
    char* c = ctime(&t2);
    return c;
}

void show_ending_messages(bool show_only)
{
    using namespace chrono;

    if (not show_only or log_verbose >= 2) {

        extern long total_reductions;
        extern long total_changeable_eval;
        extern long total_changeable_eval_with_result;
        extern long total_changeable_eval_with_call;
        extern long total_changeable_reductions;
        extern long total_reg_allocations;
        extern long total_comp_allocations;
        extern long total_step_allocations;
        extern long total_destroy_token;
        extern long total_release_knuckle;
        extern long total_create_context1;
        extern long total_create_context2;
        extern long total_tokens;
        extern long max_version;
        extern long total_reroot;
        extern long total_reroot_one;
        extern long total_set_reg_value;
        extern long total_get_reg_value;
        extern long total_get_reg_value_non_const;
        extern long total_get_reg_value_non_const_with_result;
        extern long total_invalidate;
        extern long total_steps_invalidated;
        extern long total_results_invalidated;
        extern long total_steps_scanned;
        extern long total_results_scanned;
        extern long total_steps_pivoted;
        extern long total_results_pivoted;
        extern long total_context_pr;
        extern long total_gc;
        extern long total_regs;
        extern long total_steps;

        extern long total_case_op;
        extern long total_let_op;
        extern long total_index_op;
        if (total_reductions > 0)
        {
            cout<<"total changeable evals         = "<<total_changeable_eval<<endl;
            cout<<"  with result                  = "<<total_changeable_eval_with_result<<endl;
            cout<<"  with call but not result     = "<<total_changeable_eval_with_call<<endl;
            cout<<"total reduction steps          = "<<total_reductions<<endl;
            cout<<"  changeable reduction steps   = "<<total_changeable_reductions<<endl;
            cout<<"  unchangeable reduction steps = "<<total_reductions-total_changeable_reductions<<endl;
            cout<<"  op:case                      = "<<total_case_op<<endl;
            cout<<"  op:let                       = "<<total_let_op<<endl;
            cout<<"  op:index                     = "<<total_index_op<<endl;
            cout<<"\ntotal garbage collection runs  = "<<total_gc<<endl;
            cout<<"total register allocations     = "<<total_reg_allocations<<endl;
            cout<<"total computation allocations  = "<<total_comp_allocations<<endl;
            cout<<"total step allocations         = "<<total_step_allocations<<endl;
            cout<<"total regs                     = "<<total_regs<<endl;
            cout<<"total steps                    = "<<total_steps<<endl;
            cout<<"\ntotal reroot operations        = "<<total_reroot<<endl;
            cout<<"  total atomic reroot          = "<<total_reroot_one<<endl;
            cout<<"  total steps pivoted          = "<<total_steps_pivoted<<endl;
            cout<<"  total results pivoted        = "<<total_results_pivoted<<endl;
            cout<<"total invalidations            = "<<total_invalidate<<endl;
            cout<<"  total steps invalidated      = "<<total_steps_invalidated<<endl;
            cout<<"  total results invalidated    = "<<total_results_invalidated<<endl;
            cout<<"  total steps scanned          = "<<total_steps_scanned<<endl;
            cout<<"  total results scanned        = "<<total_results_scanned<<endl;
            cout<<"total tokens                   = "<<total_tokens<<endl;
            cout<<"maximum version                = "<<max_version<<endl;
            cout<<"total tokens destroyed         = "<<total_destroy_token<<endl;
            cout<<"  total knuckles destroyed     = "<<total_release_knuckle<<endl;
            cout<<"total create context           = "<<total_create_context1+total_create_context2<<endl;
            cout<<"  operator=                    = "<<total_create_context1<<endl;
            cout<<"  copy constructor             = "<<total_create_context2<<endl;
            cout<<"\ntotal values set               = "<<total_set_reg_value<<endl;
            cout<<"total values gotten            = "<<total_get_reg_value<<endl;
            cout<<"total values gotten variable   = "<<total_get_reg_value_non_const<<endl;
            cout<<"total values gotten w/ result  = "<<total_get_reg_value_non_const_with_result<<endl;
            cout<<"total context probability      = "<<total_context_pr<<endl;
        }
    }
    system_clock::time_point end_time = system_clock::now();
  
    if (end_time - start_time > seconds(2)) 
    {
        cout<<endl;
        cout<<"start time: "<<ctime(start_time)<<endl;
        cout<<"  end time: "<<ctime(end_time)<<endl;
        cout<<"total (elapsed) time: "<<duration_string( duration_cast<seconds>(end_time-start_time) )<<endl;
        cout<<"total (CPU) time: "<<duration_string( duration_cast<seconds>(total_cpu_time()) )<<endl;
    }
    if (substitution::total_calc_root_prob > 1 and not show_only) {
        cout<<endl;
        cout<<"total likelihood evals = "<<substitution::total_likelihood<<endl;
        cout<<"total calc_root_prob evals = "<<substitution::total_calc_root_prob<<endl;
        cout<<"total branches peeled = "<<substitution::total_peel_leaf_branches+substitution::total_peel_internal_branches<<endl;
        cout<<"average root clv length = "<<substitution::total_root_clv_length/substitution::total_calc_root_prob<<endl;
        cout<<endl;
    }
}

std::shared_ptr<module_loader> setup_module_loader(variables_map& args, const string& argv0)
{
    std::shared_ptr<module_loader> L (new module_loader(get_package_paths(argv0, args) ));

    // 4. Write out paths to C1.err
    if (log_verbose >= 1)
    {
        std::cout<<"\nPackage path = \n";
        for(const auto& path: L->plugins_path)
            cout<<"  "<<path<<"\n";
        cout<<std::endl;
    }

    // 5a. Check for empty paths
    if (L->plugins_path.empty())
        throw myexception()<<"No plugin paths are specified!.  Use --package-path=<path> to specify the directory containing 'Prelude"<<plugin_extension<<"'.";

    // 5b. Check for Prelude.so
    try
    {
        L->find_plugin("Prelude");
    }
    catch (...)
    {
        throw myexception()<<"Can't find Prelude plugin.  Use --package-path=<path> to specify the directory containing 'Prelude"<<plugin_extension<<"'.";
    }

    // 5c. Check for Prelude.hs
    try
    {
        L->find_module("Prelude");
    }
    catch (...)
    {
        throw myexception()<<"Can't find Prelude in module path.  Use --package-path=<path> to specify the directory containing 'modules/Prelude.hs'.";
    }

    L->pre_inline_unconditionally = args["pre-inline"].as<bool>();
    L->post_inline_unconditionally = args["post-inline"].as<bool>();
    L->let_float_from_case = args["let-float-from-case"].as<bool>();
    L->let_float_from_apply = args["let-float-from-apply"].as<bool>();
    L->let_float_from_let = args["let-float-from-let"].as<bool>();
    L->case_of_constant = args["case-of-constant"].as<bool>();
    L->case_of_variable = args["case-of-variable"].as<bool>();
    L->case_of_case = args["case-of-case"].as<bool>();
    L->inline_threshhold = args["inline-threshold"].as<int>();
    L->keenness = args["keenness"].as<double>();
    L->beta_reduction = args["beta-reduction"].as<bool>();
    L->max_iterations = args["simplifier-max-iterations"].as<int>();

    L->fully_lazy = args["fully-lazy"].as<bool>();
    L->dump_parsed = args.count("dump-parsed");
    L->dump_renamed = args.count("dump-rn");
    L->dump_desugared = args.count("dump-ds");

    return L;
}

std::string generate_print_program(const model_t& print, const expression_ref& a)
{
    std::ostringstream program_file;

    program_file<<"-- Use the program `brittany` (or `hindent`) to indent this file for readability\n";
    set<string> imports;
    imports.insert("Probability.Random");     // for run_lazy
    imports.insert("Alphabet");               // for dna
    add(imports, print.imports);
    for(auto& mod: imports)
        program_file<<"import "<<mod<<"\n";

    program_file<<"\n";
    program_file<<"print_model = "<<print.expression.print()<<"\n";
    program_file<<"\n";
    program_file<<"alphabet = "<<a.print()<<"\n";
    program_file<<"\n";
    program_file<<"main = do\n";
    program_file<<"  result <- run_lazy alphabet print_model\n";
    program_file<<"  putStrLn $ show $ fst result\n";

    return program_file.str();
}

expression_ref get_alphabet_expression_from_args(const variables_map& args)
{
    expression_ref a = {var("error"),String("No alphabet!")};
    if (args.count("alphabet"))
    {
        auto as = args["alphabet"].as<vector<string>>();
        if (as.size() > 1)
            throw myexception()<<"Only a single alphabet can be used with --print!";
        auto alpha = as[0];
        if (as.size() == 1)
        {
            if (alpha == "DNA")
                a = var("dna");
            else if (alpha == "RNA")
                a = var("rna");
            else if (alpha == "AA")
                a = var("amino_acids");
            else if (alpha == "Doublets")
                a = {var("doublets"),var("rna")};
            else if (alpha == "Triplets")
                a = {var("triplets"),var("dna")};
            else if (alpha == "Codons")
                a = {var("codons"),var("dna"),var("standard_code")};
        }
    }
    return a;
}

void run_print_expression(const string& argv0, variables_map& args, const shared_ptr<module_loader>& L)
{
    const string mstring = args["print"].as<string>();
    Rules R(get_package_paths(argv0, args));
    model_t print = get_model(R,"a",mstring);

    expression_ref a = get_alphabet_expression_from_args(args);
    {
        checked_ofstream program_file("Print.Main.hs");
        program_file<<generate_print_program(print, a);
    }
    execute_file(L, "Print.Main.hs");
}

int simple_size(const expression_ref& E);

int main(int argc,char* argv[])
{ 
    int n_procs = 1;
    int proc_id = 0;

#ifdef HAVE_MPI
    mpi::environment env(argc, argv);
    mpi::communicator world;

    proc_id = world.rank();
    n_procs = world.size();
#endif

    restore restore_cout(cout);
    restore restore_cerr(cerr);
    restore restore_clog(clog);

    std::ios::sync_with_stdio(false);

    ostream out_screen(cout.rdbuf());
    ostream err_screen(cerr.rdbuf());

    std::ostringstream out_cache;
    std::ostringstream err_cache;

    vector<shared_ptr<ostream>> files;

    teebuf tee_out(out_screen.rdbuf(), out_cache.rdbuf());
    teebuf tee_err(err_screen.rdbuf(), err_cache.rdbuf());

    ostream out_both(&tee_out);
    ostream err_both(&tee_err);

    int retval=0;

    bool show_only = false;

    owned_ptr<Model> M; // Declare early so we can reference from catch block.

    try {

#if defined(HAVE_FEENABLEEXCEPT) && !defined(NDEBUG)
        feenableexcept(FE_DIVBYZERO|FE_OVERFLOW|FE_INVALID);
        //    feclearexcept(FE_DIVBYZERO|FE_OVERFLOW|FE_INVALID);
#endif
#if defined(HAVE_CLEAREXCEPT) && defined(NDEBUG)
        feclearexcept(FE_DIVBYZERO|FE_OVERFLOW|FE_INVALID);
#endif
        fp_scale::initialize();

        //---------- Parse command line  ---------//
        variables_map args = parse_cmd_line(argc,argv);

        show_only = args.count("test");

        //------ Increase precision for (cout,cerr) if we are testing ------//
        if (args.count("test"))
        {
            cerr.precision(15);
            cout.precision(15);
        }
        else
        //------ Capture copy of 'cerr' output in 'err_cache' ------//
            cerr.rdbuf(err_both.rdbuf());

        //------------- Setup module loader -------------//
        auto L = setup_module_loader(args, argv[0]);
        L->args = trailing_args(argc, argv, trailing_args_separator);

        //---------- Initialize random seed -----------//
        unsigned long seed = init_rng_and_get_seed(args);
    
        if (log_verbose >= 1) out_cache<<"random seed = "<<seed<<endl<<endl;

        //---------- test optimizer ----------------
        if (args.count("test-module"))
        {
            string filename = args["test-module"].as<string>();
            auto M = L->load_module_from_file(filename);

            Program P(L);
            P.add(M);
            auto& M2 = P.get_module(M.name);
            for(const auto& [name, body]: M2.code_defs())
            {
                std::cerr<<"size = "<<simple_size(body)<<"   "<<name<<" = "<<body<<std::endl;
            }
            exit(0);
        }
        else if (args.count("run-module"))
        {
            string filename = args["run-module"].as<string>();
            execute_file(L, filename);
            exit(0);
        }
        else if (args.count("print"))
        {
            run_print_expression(argv[0], args, L);
            exit(0);
        }

        //----------- Create output dir --------------//
        fs::path output_dir = fs::current_path();
        if (not args.count("test")) {
#ifdef HAVE_MPI
            // FIXME: Can we just use `broadcast(world, output_dir, 0)`?
            //        This might require a serializer for fs::path.
            if (not proc_id) {
                output_dir = init_dir(args);

                for(int dest=1;dest<n_procs;dest++) 
                    world.send(dest, 0, output_dir.string());
            }
            else
            {
                string dir_name;
                world.recv(0, 0, dir_name);
                output_dir = dir_name;
            }

            // cerr<<"Proc "<<proc_id<<": dirname = "<<dir_name<<endl;
#else
            output_dir = init_dir(args);
#endif
        }

        //---------- Create model object -----------//
        json info;
        run_info(info, proc_id, argc, argv);
        info["seed"] = seed;

        if (args.count("align"))
        {
            Rules R(get_package_paths(argv[0], args));
            M = create_A_and_T_model(R, args, L, out_cache, out_screen, out_both, info, proc_id, output_dir.string());

            if (args.count("tree") and M.as<Parameters>())
            {
                auto P = M.as<Parameters>();
                for(int i=0;i<P->n_branch_scales();i++)
                    if (P->branch_scale(i).is_modifiable(*P))
                        P->set_branch_scale(i, 1.0);
            }
        }
        else
        {
            Model::key_map_t keys;
            if (args.count("set"))
                keys = parse_key_map(args["set"].as<vector<string> >());

            Program P(L, Program::exe_type::log_list);
            if (args.count("model"))
            {
                auto filename = args["model"].as<string>();
                auto m = P.get_module_loader()->load_module_from_file(filename);
                P.add(m);
                P.main = m.name + ".main";
            }
            else if (args.count("Model"))
            {
                auto module_name = args["Model"].as<string>();
                P.add(module_name);
                P.main = module_name + ".main";
            }
            else
                std::abort();

            M = Model(P, keys);
        }
        L.reset();

        //------------ Avoid printing seed during unrelated error messages ---//
        if (log_verbose < 1) out_cache<<"random seed = "<<seed<<endl<<endl;

        //---------------Do something------------------//
        vector<string> Rao_Blackwellize;
        if (args.count("Rao-Blackwellize"))
            Rao_Blackwellize = split(args["Rao-Blackwellize"].as<string>(),',');

        if (args.count("test"))
        {
            auto TL = construct_table_function(M, Rao_Blackwellize);

            auto log_formats = get_log_formats(args,(bool)M.as<Parameters>());
            if (log_formats.count("tsv"))
                std::cout<<table_logger_line(*TL, *M, 0)<<"\n";
            if (log_formats.count("json"))
                std::cout<<logged_params_and_some_computed_stuff(*M, 0)<<"\n";
            M->show_graph();
        }
        else 
        {
            raise_cpu_limit(out_both);

            block_signals();

            long int max_iterations = 200000;
            if (args.count("iterations"))
                max_iterations = args["iterations"].as<long int>();
            int subsample = args["subsample"].as<int>();
      
            //---------- Open output files -----------//
            vector<MCMC::Logger> loggers;

            if (not args.count("test")) {
                info["subdirectory"] = output_dir.string();
                files = init_files(proc_id, output_dir.string(), argc, argv);
                loggers = construct_loggers(args, M, subsample, Rao_Blackwellize, proc_id, output_dir.string());

                if (args.count("align"))
                    write_initial_alignments(args, proc_id, output_dir.string());
            }
            else {
                files.push_back(shared_ptr<ostream>(new ostream(cout.rdbuf())));
                files.push_back(shared_ptr<ostream>(new ostream(cerr.rdbuf())));
            }

            M->clear_program();
            M->clear_identifiers();

            //------ Redirect output to files -------//
            *files[0]<<out_cache.str(); out_cache.str("");
            *files[1]<<err_cache.str(); err_cache.str("");

            tee_out.setbuf2(files[0]->rdbuf());
            tee_err.setbuf2(files[1]->rdbuf());

            cout.flush() ; cout.rdbuf(files[0]->rdbuf());
            cerr.flush() ; cerr.rdbuf(files[1]->rdbuf());
            clog.flush() ; clog.rdbuf(files[1]->rdbuf());

            //------ Write run info to C1.json ------//
            *files[2]<<info.dump(4)<<std::endl;

            //------ Redirect output to files -------//

            avoid_zero_likelihood(M, *files[0], out_both);

            do_pre_burnin(args, M, *files[0], out_both);

            out_screen<<"\nBAli-Phy does NOT detect how many iterations is sufficient:\n   You need to monitor convergence and kill it when done."<<endl;
            if (not args.count("iterations"))
                out_screen<<"   Maximum number of iterations not specified: limiting to "<<max_iterations<<"."<<endl;
            else
                out_screen<<"   Maximum number of iterations set to "<<max_iterations<<"."<<endl;

            out_screen<<"\nBeginning MCMC computations."<<endl;
            out_screen<<"   - Future screen output sent to '"<<output_dir.string()<<"/C1.out'"<<endl;
            out_screen<<"   - Future debugging output sent to '"<<output_dir.string()<<"/C1.err'"<<endl;
            if (M.as<Parameters>())
            {
                out_screen<<"   - Sampled trees logged to '"<<output_dir.string()<<"/C1.trees'"<<endl;
                out_screen<<"   - Sampled alignments logged to '"<<output_dir.string()<<"/C1.P<partition>.fastas'"<<endl;
                out_screen<<"   - Run info written to '"<<output_dir.string()<<"/C1.run.json'"<<endl;
            }
            auto log_formats = get_log_formats(args,(bool)M.as<Parameters>());
            if (log_formats.count("json"))
                out_screen<<"   - Sampled numerical parameters logged to '"<<output_dir.string()<<"/C1.log.json' as JSON\n";
            if (log_formats.count("tsv"))
                out_screen<<"   - Sampled numerical parameters logged to '"<<output_dir.string()<<"/C1.log' as TSV\n";
            out_screen<<"\n";
            if (log_formats.count("tsv"))
                out_screen<<"You can examine 'C1.log' using BAli-Phy tool statreport (command-line) or the BEAST program Tracer (graphical).\n";
            out_screen<<"See the manual at http://www.bali-phy.org/README.xhtml for further information.\n";
            out_screen.flush();

            //-------- Start the MCMC  -----------//
            do_sampling(args, M, max_iterations, *files[0], loggers);

            // Close all the streams, and write a notification that we finished all the iterations.
            // close_files(files);
        }
    }
    catch (std::bad_alloc&) 
    {
        // 1. If we haven't yet moved screen output to a file, then write cached screen output.
        out_screen<<out_cache.str(); out_screen.flush();
        err_screen<<err_cache.str(); err_screen.flush();

        // 2. Now, write message to either (screen+cache) or (screen+file), and flush.
        err_both<<"Doh!  Some kind of memory problem?\n"<<endl;
        // 3. Write memory report to either (screen) or (screen+cache) or (screen+file)
        report_mem();
        retval=2;
    }
    catch (std::exception& e) 
    {
        // 1. If we haven't yet moved screen output to a file, then write cached screen output.
        out_screen<<out_cache.str(); out_screen.flush();
        err_screen<<err_cache.str(); err_screen.flush();

        // 2. Now, write message to either (screen+cache) or (screen+file), and flush.
        if (n_procs > 1)
            err_both<<"bali-phy: Error["<<proc_id<<"]! "<<e.what()<<endl;
        else
            err_both<<"bali-phy: Error! "<<e.what()<<endl;

        retval=1;

        if (log_verbose > 0 and M)
            M->show_graph();
    }

    show_ending_messages(show_only);

    out_both.flush();
    err_both.flush();
    return retval;
}

