#include <iostream>
#include <algorithm>
#include "util/truncate.H"
#include "graph_register.H"
#include "computation/expression/var.H"
#include "computation/expression/reg_var.H"
#include "computation/expression/tuple.H"
#include "computation/expression/random_variable.H"
#include "computation/expression/modifiable.H"
#include "computation/operations.H"

#define DEBUG_MACHINE 2

using std::string;
using std::vector;
using std::pair;

using std::cerr;
using std::endl;

using std::optional;

long total_reg_allocations = 0;
long total_step_allocations = 0;
long total_comp_allocations = 0;
long total_set_reg_value = 0;
long total_get_reg_value = 0;
long total_get_reg_value_non_const = 0;
long total_get_reg_value_non_const_with_result = 0;
long total_context_pr = 0;
long total_tokens = 0;
long max_version = 0;

/*
 * Goal: Share computation of WHNF structures between contexts, even when those
 *       stuctures are uncomputed at the time the contexts are split.
 *
 *       Rolling back to a previous context should not require recomputing anything
 *       that was previously known, and should take advantage of anything we computed
 *       for the next context that is also used by the old one.
 *
 * In order to share partially evaluated expressions between contexts, we need
 * these contexts to share a memory, since constructor expressions reference other
 * entries in the memory.
 *
 * Forward edges consist of
 * - E edges
 * - used edges (forward: used_inputs, backward: used_by)
 * - call edges (forward: call, backward: called_by)
 * - value edges (computed by following call edges).
 * The called_by back edges indicate that a value is being used by another value that calls us.
 * Thus called_by edges need not be set when setting a call, but only when setting the value.
 */

/*
 * 1. get_reg_value( )... can we avoid re-rooting?
 *
 * 2. set_reg_value( ): speedup?
 *
 * 3. registering modifiables... can we just create a list inside reg_heap?
 *
 * 4. how could we *dynamically* handle modifiables
 *    - we need to make an MCMC move more them.
 *    - we need to incorporate them into the PDF
 */

/*
 * 1. [DONE] Make the root token into token 0.
 *
 * 2. [DONE] Remove the idea of an unchangable token.
 *
 * 3. [DONE] Make let into an operation.
 *
 * 4. [DONE] Remove t argument from computation_index_for_reg(int t, int r) 
 *
 * 5. Clean up back-edges to computations when computations are destroyed.
 *
 * 6. Move call and used_inputs into reduction
 *
 * 7. Make back-edges from reduction to computations that use it.
 *    - remove duplicate_computation( ).
 *    - HOW does this affect the invalidation algorithm??
 *
 * 8. 
 *
 */

/*
 * OK, so when we invalidate a modifiable, we also unref any local computations that
 * depend on this.  When we destroy a computation, we know that no computation can reference
 * its call -- or, in fact, any reg that the computation created.
 *
 * A. Therefore, we can do brute-force GC on the called reg: we scan all tokens and remove any 
 *    computations for the called reg.  This will lead to MORE regs being freed.  We therefore
 *    loop until no more regs (and thus computations) are being freed.
 */

void Step::clear()
{
    source_reg = -1;
    call = 0;
    truncate(used_inputs);
    truncate(forced_regs);
    assert(created_regs.empty());

    // This should already be cleared.
    assert(flags.none());
}

void Step::check_cleared()
{
    assert(not call);
    assert(used_inputs.empty());
    assert(forced_regs.empty());
    assert(created_regs.empty());
    assert(flags.none());
}

Step& Step::operator=(Step&& S) noexcept
{
    source_reg = S.source_reg;
    call = S.call;
    used_inputs  = std::move( S.used_inputs );
    forced_regs  = std::move( S.forced_regs );
    created_regs  = std::move( S.created_regs );
    flags = S.flags;

    return *this;
}

Step::Step(Step&& S) noexcept
:source_reg( S.source_reg),
 call ( S.call ),
 used_inputs ( std::move(S.used_inputs) ),
 forced_regs (std::move(S.forced_regs) ),
 created_regs ( std::move(S.created_regs) ),
 flags ( S.flags )
{ }

void Result::clear()
{
    source_step = -1;
    source_reg = -1;
    value = 0;
    truncate(call_edge);
    truncate(used_by);
    truncate(called_by);

    // This should already be cleared.
    assert(flags.none());
}

void Result::check_cleared()
{
    assert(not value);
    assert(not call_edge.first);
    assert(called_by.empty());
    assert(used_by.empty());
    assert(flags.none());
}

Result& Result::operator=(Result&& R) noexcept
{
    value = R.value;
    source_step = R.source_step;
    source_reg = R.source_reg;
    call_edge = R.call_edge;
    used_by = std::move( R.used_by );
    called_by = std::move( R.called_by );
    flags = R.flags;

    return *this;
}

Result::Result(Result&& R) noexcept
:source_step(R.source_step),
                         source_reg(R.source_reg),
                         value (R.value), 
                         call_edge (R.call_edge),
                         used_by ( std::move( R.used_by) ),
                         called_by ( std::move( R.called_by) ),
                         flags ( R.flags )
{ }

void Force::clear()
{
    source_step = -1;
    source_result = -1;
    source_reg = -1;
    truncate(forced_inputs);
    truncate(forced_by);
}

void Force::check_cleared()
{
    assert(forced_inputs.empty());
    assert(forced_by.empty());
}

Force& Force::operator=(Force&& F) noexcept
{
    source_step = F.source_step;
    source_result = F.source_result;
    source_reg = F.source_reg;
    forced_inputs = std::move( F.forced_inputs );
    forced_by = std::move( F.forced_by );

    return *this;
}

Force::Force(Force&& F) noexcept
{
    operator=(std::move(F));
}

reg& reg::operator=(reg&& R) noexcept
{
    C = std::move(R.C);

    type = R.type;

    created_by = std::move(R.created_by);

    flags = R.flags;

    return *this;
}

reg::reg(reg&& R) noexcept
:C( std::move(R.C) ),
 type ( R.type ),
 created_by( std::move(R.created_by) ),
 flags ( R.flags )
{ }

void reg::clear()
{
    C.clear();
    type = type_t::unknown;
    created_by = {0,0};
    flags.reset();
}

void reg::check_cleared()
{
    assert(not C);
    assert(type == type_t::unknown);
    assert(created_by.first == 0);
    assert(created_by.second == 0);
    assert(flags.none());
}

std::optional<int> reg_heap::creator_of_reg(int r) const
{
    int s = regs[r].created_by.first;
    assert(s >= 0);
    if (s == 0)
        return {};
    else
        return s;
}

bool reg_heap::reg_is_contingent(int r) const
{
    return (bool)creator_of_reg(r);
}

bool reg_heap::step_exists_in_root(int s) const
{
    assert(s > 0);
    int r = steps[s].source_reg;
    assert(r > 0 and r < size());
    return prog_steps[r] == s;
}

bool reg_heap::reg_exists(int r) const
{
    auto s = creator_of_reg(r);
    if (not s)
        return true;
    else
        return step_exists_in_root(*s);
}

size_t reg_heap::size() const
{
    assert(regs.size() == prog_steps.size());
    assert(regs.size() == prog_results.size());
    assert(regs.size() == prog_temp.size());
    return regs.size();
}

void reg_heap::register_prior(int r)
{
    r = follow_index_var(r);

    if (not reg_has_value(r))
        throw myexception()<<"Can't register a prior reg that is unevaluated!";

    if (regs.access(r).flags.test(0))
        throw myexception()<<"Can't register a prior reg that is already registered!";

    if (reg_is_constant(r))
    {
        // Also avoid putting a bit on constant regs?
    }
    else
    {
        regs.access(r).flags.set(0);

        assert(reg_is_changeable(r));
    }
}

void reg_heap::register_likelihood_(int r)
{
    r = follow_index_var(r);

    if (not reg_has_value(r))
        throw myexception()<<"Can't register a likelihood reg that is unevaluated!";

    if (regs.access(r).flags.test(1))
        throw myexception()<<"Can't register a likelihood reg that is already registered!";

    // We can only put the bit on a changeable reg, not on (say) an index_var.
    // Therefore, we must evaluate r -> r2 here.
    // QUESTION: WHY can't we put the bit on constant regs?

    if (reg_is_constant(r))
    {
        // Also avoid putting a bit on constant regs?

        likelihood_heads.push_back(r);
    }
    else
    {
        regs.access(r).flags.set(1);

        assert(reg_is_changeable(r));

        likelihood_heads.push_back(r);
    }
}

void reg_heap::register_likelihood(int r)
{
    mark_completely_dirty(root_token);
    auto [r2,v] = incremental_evaluate(r, true);

    register_likelihood_(r2);
}

int reg_heap::register_likelihood(closure&& C)
{
    assert(not C.exp.head().is_a<expression>());

    int r = allocate();
    set_C(r, std::move(C));
    register_likelihood(r);
    return r;
}

log_double_t reg_heap::probability_for_context(int c)
{
    total_context_pr++;

    return prior_for_context(c) * likelihood_for_context(c);
}

int reg_heap::follow_index_var(int r) const
{
    while((*this)[r].exp.is_index_var())
        r = (*this)[r].reg_for_index_var();
    return r;
}

expression_ref reg_heap::evaluate_program(int c)
{
    if (not program_result_head)
        throw myexception()<<"No program has been set!";

    auto result = lazy_evaluate(heads[*program_result_head], c, true).exp;

    // Check that all the priors and likelihoods are forced.
#ifndef NDEBUG
    for(int r_likelihood: likelihood_heads)
    {
        assert(reg_exists(r_likelihood));
        assert(reg_has_value(follow_index_var(r_likelihood)));
    }

    for(int r_rv: random_variables_)
    {
        assert(reg_exists(r_rv));

        int r_pdf = (*this)[r_rv].reg_for_slot(1);
        assert(reg_exists(r_pdf));

        assert(reg_has_value(follow_index_var(r_pdf)));
    }
#endif

    return result;
}

prob_ratios_t reg_heap::probability_ratios(int c1, int c2)
{
#if DEBUG_MACHINE >= 2
    for(auto x : prog_temp)
        assert(x.none());
#endif

    constexpr int pdf_bit = 7;

    // 1. reroot to c1 and force the program
    evaluate_program(c1);

    // 2. install another reroot handler
    vector<pair<int,int>> original_pdf_results;

    std::function<void(int)> handler = [&original_pdf_results,this](int old_root)
    {
        for(auto& p: tokens[old_root].delta_result())
        {
            int r =  p.first;

            // We're only interested in cases where both contexts have a result that is > 0.
            // But (i) we need to seen the "seen" flag in any case
            //    (ii) we need to remember that we have set it so that we can unset it.
            if (regs.is_used(r) and regs.access(r).flags.any() and not prog_temp[r].test(pdf_bit))
            {
                prog_temp[r].set(pdf_bit);
                original_pdf_results.push_back(p);
            }
        }
    };

    reroot_handlers.push_back(handler);

    // 3. reroot to c2 and force the program
    evaluate_program(c2);

    // 4. compute the ratio only for (i) changed pdfs that (ii) exist in both c1 and c2
    prob_ratios_t R{1.0, 1.0, false};

    for(auto [pdf_reg, pdf_res]: original_pdf_results)
    {
        assert(prog_temp[pdf_reg].test(pdf_bit));

        prog_temp[pdf_reg].reset(pdf_bit);

        // Only compute a ratio if the pdf is present and computed in BOTH contexts.
        if (pdf_res > 0 and has_result(pdf_reg))
        {
            int result_reg1 = results[pdf_res].value;
            int result_reg2 = result_value_for_reg(pdf_reg);
            log_double_t r = (*this)[result_reg2].exp.as_log_double() / (*this)[result_reg1].exp.as_log_double();

            assert(regs.access(pdf_reg).flags.test(0) or regs.access(pdf_reg).flags.test(1));
            if (regs.access(pdf_reg).flags.test(0))
                R.prior_ratio *= r;
            else 
                R.likelihood_ratio *= r;
        }
    }

#if DEBUG_MACHINE >= 2
    for(auto x : prog_temp)
        assert(x.none());
#endif

    // 5. remove the reroot handler
    reroot_handlers.pop_back();

//  If pr1 and pr2 are off far enough, this test will fail...
//    if (pr1 > 0.0 and pr2 > 0.0)
//      assert( std::abs( (pr2/pr1).log() - R.prior_ratio.log() - R.likelihood_ratio.log()) < 1.0e-4 );

    return R;
}

void reg_heap::register_random_variable(int r)
{
    auto [r2,_] = incremental_evaluate(r, true);
    r = r2;

    if (not is_random_variable(expression_at(r)))
        throw myexception()<<"Trying to register `"<<expression_at(r)<<"` as random variable";
    random_variables_.push_back(r);

    int r_pdf = (*this)[r].reg_for_slot(1);
    register_prior(r_pdf);
}

const vector<int>& reg_heap::random_variables() const
{
    return random_variables_;
}

optional<int> reg_heap::parameter_is_modifiable_reg(int index)
{
    int& R = parameters[index].second;

    return find_update_modifiable_reg(R);
}

optional<int> reg_heap::compute_expression_is_modifiable_reg(int index)
{
    int& H = heads[index];

    return find_update_modifiable_reg(H);
}

optional<int> reg_heap::find_update_modifiable_reg(int& R)
{
    // Note: here we always update R
    R = incremental_evaluate_unchangeable(R);

    auto& C = (*this)[R];

    if (is_modifiable(C.exp))
        return R;
    else if (is_random_variable(C.exp))
    {
        int R2 = C.reg_for_slot(0);
        return find_update_modifiable_reg(R2);
    }
    else if (is_seq(C.exp))
    {
        int R2 = C.reg_for_slot(1);
        return find_update_modifiable_reg(R2);
    }
    else if (is_join(C.exp))
    {
        int R2 = C.reg_for_slot(1);
        return find_update_modifiable_reg(R2);
    }
    else
        return {};
}

optional<int> reg_heap::find_modifiable_reg(int R)
{
    return find_update_modifiable_reg(R);
}

optional<int> reg_heap::parameter_is_random_variable(int index)
{
    int& R = parameters[index].second;

    return find_update_random_variable(R);
}

optional<int> reg_heap::compute_expression_is_random_variable(int index)
{
    int& H = heads[index];

    return find_update_random_variable(H);
}

optional<int> reg_heap::find_update_random_variable(int& R)
{
    // Note: here we always update R
    R = incremental_evaluate_unchangeable(R);

    if (is_random_variable(expression_at(R)))
        return R;
    else
        return {};
}

optional<int> reg_heap::find_random_variable(int R)
{
    return find_update_random_variable(R);
}

const expression_ref reg_heap::get_parameter_range(int c, int p)
{
    if (auto rv = parameter_is_random_variable(p))
        return get_range_for_random_variable(c, *rv);
    else
        return {};
}

const expression_ref reg_heap::get_range_for_random_variable(int c, int r)
{
    if (find_update_random_variable(r))
    {
        int r_range = closure_at(r).reg_for_slot(3);
        return get_reg_value_in_context(r_range, c);
    }
    else
        throw myexception()<<"Trying to get range from `"<<closure_at(r).print()<<"`, which is not a random_variable!";
}

double reg_heap::get_rate_for_random_variable(int c, int r)
{
    if (find_update_random_variable(r))
    {
        int r_rate = closure_at(r).reg_for_slot(4);
        return get_reg_value_in_context(r_rate, c).as_double();
    }
    else
        throw myexception()<<"Trying to get rate from `"<<closure_at(r).print()<<"`, which is not a random_variable!";
}

int reg_heap::step_index_for_reg(int r) const 
{
    assert(prog_steps[r] != 0);
    return prog_steps[r];
}

int reg_heap::result_index_for_reg(int r) const 
{
    assert(prog_results[r] != 0);
    return prog_results[r];
}

int reg_heap::force_index_for_reg(int r) const
{
    assert(prog_force[r] != 0);
    return prog_force[r];
}

const Step& reg_heap::step_for_reg(int r) const 
{ 
    int s = step_index_for_reg(r);
    return steps.access_unused(s);
}

Step& reg_heap::step_for_reg(int r)
{ 
    int s = step_index_for_reg(r);
    return steps.access_unused(s);
}

const Result& reg_heap::result_for_reg(int r) const 
{ 
    int res = result_index_for_reg(r);
    return results.access_unused(res);
}

Result& reg_heap::result_for_reg(int r)
{ 
    int res = result_index_for_reg(r);
    return results.access_unused(res);
}

const Force& reg_heap::force_for_reg(int r) const
{
    int f = force_index_for_reg(r);
    return forces.access_unused(f);
}

Force& reg_heap::force_for_reg(int r)
{
    int f = force_index_for_reg(r);
    return forces.access_unused(f);
}

const closure& reg_heap::access_value_for_reg(int R1) const
{
    int R2 = value_for_reg(R1);
    return closure_at(R2);
}

bool reg_heap::reg_has_value(int r) const
{
    if (regs.access(r).type == reg::type_t::constant)
        return true;
    else
        return reg_has_result_value(r);
}

bool reg_heap::reg_has_result_value(int r) const
{
    return has_result(r) and result_value_for_reg(r);
}

bool reg_heap::reg_has_call(int r) const
{
    return has_step(r) and call_for_reg(r);
}

int reg_heap::call_for_reg(int r) const
{
    return step_for_reg(r).call;
}

bool reg_heap::has_step(int r) const
{
    return step_index_for_reg(r)>0;
}

bool reg_heap::has_result(int r) const
{
    return result_index_for_reg(r)>0;
}

bool reg_heap::has_force(int r) const
{
    return force_index_for_reg(r)>0;
}

void reg_heap::force_reg(int r)
{
    assert(reg_is_changeable(r));
    assert(has_step(r));
    assert(has_result(r));
    assert(not has_force(r));

    int s = step_index_for_reg(r);
    int res = result_index_for_reg(r);

    int f = add_shared_force(r, s, res);

    const auto& S = steps.access(s);

    for(auto& [result,_]: S.used_inputs)
    {
        int r2 = results.access(result).source_reg;
        incremental_evaluate(r2, true);
        set_forced_input2(f,r2);
    }

    for(auto r2: S.forced_regs)
    {
        incremental_evaluate(r2, true);
        assert(has_force(r2));
        assert(reg_has_result_value(r2));

        set_forced_input2(f,r2);
    }

    assert(S.call > 0);

    incremental_evaluate(S.call, true);

    // If R2 is WHNF then we are done
    if (not reg_is_constant(S.call))
    {
        assert(reg_is_changeable(S.call));
        set_forced_input2(f, S.call);
    }

    assert(force_for_reg(r).forced_inputs.size() >= S.forced_regs.size());
}

bool reg_heap::unforced_reg(int r) const
{
    return reg_is_changeable(r) and (not has_force(r));
}

int reg_heap::value_for_reg(int r) const 
{
    assert(not (*this)[r].exp.is_index_var());
    if (regs.access(r).type == reg::type_t::changeable)
        return result_value_for_reg(r);
    else
    {
        assert(regs.access(r).type == reg::type_t::constant);
        return r;
    }
}

int reg_heap::result_value_for_reg(int r) const 
{
    return result_for_reg(r).value;
}

void reg_heap::set_result_value_for_reg(int r1)
{
    int call = call_for_reg(r1);

    assert(call);

    int value = value_for_reg(call);

    assert(value);

    int res1 = result_index_for_reg(r1);
    if (res1 < 0)
        res1 = add_shared_result(r1, step_index_for_reg(r1));
    assert(res1 > 0);
    auto& RES1 = results[res1];
    RES1.value = value;

    // If R2 is WHNF then we are done
    if (regs.access(call).type == reg::type_t::constant) return;

    // If R2 doesn't have a result, add one to hold the called-by edge.
    assert(has_result(call));

    // Add a called-by edge to R2.
    int res2 = result_index_for_reg(call);
    int back_index = results[res2].called_by.size();
    results[res2].called_by.push_back(res1);
    RES1.call_edge = {res2, back_index};
}

void reg_heap::set_used_input(int s1, int R2)
{
    assert(reg_is_changeable(R2));

    assert(regs.is_used(R2));

    assert(closure_at(R2));

    assert(has_result(R2));
    assert(result_value_for_reg(R2));

    // An index_var's value only changes if the thing the index-var points to also changes.
    // So, we may as well forbid using an index_var as an input.
    assert(not expression_at(R2).is_index_var());

    int res2 = result_index_for_reg(R2);

    int back_index = results[res2].used_by.size();
    int forw_index = steps[s1].used_inputs.size();
    results[res2].used_by.push_back({s1,forw_index});
    steps[s1].used_inputs.push_back({res2,back_index});

    assert(result_is_used_by(s1,res2));
}

void reg_heap::set_forced_input2(int f1, int R2)
{
    assert(reg_is_changeable(R2));

    assert(regs.is_used(R2));

    assert(closure_at(R2));

    assert(has_force(R2));

    // An index_var's value only changes if the thing the index-var points to also changes.
    // So, we may as well forbid using an index_var as an input.
    assert(not expression_at(R2).is_index_var());

    int f2 = force_index_for_reg(R2);

    int back_index = forces[f2].forced_by.size();
    int forw_index = forces[f1].forced_inputs.size();
    forces[f2].forced_by.push_back({f1,forw_index});
    forces[f1].forced_inputs.push_back({f2,back_index});
    // QUESTION: should we be tracking the forced_regs here, outside of the step?
    //   Maybe that would allow us to have a constant with effects.

    assert(force_is_forced_by(f1,f2));
}

void reg_heap::set_call(int R1, int R2)
{
    assert(reg_is_changeable(R1));
    // R2 might be of UNKNOWN changeableness

    // Check that R1 is legal
    assert(regs.is_used(R1));

    // Only modify the call for the current context;
    assert(has_step(R1));

    // Don't override an *existing* call
    assert(not reg_has_call(R1));

    // Check that we aren't overriding an existing *value*
    assert(not reg_has_value(R1));

    // Set the call
    set_call_from_step(step_index_for_reg(R1), R2);
}

void reg_heap::set_call_from_step(int s, int R2)
{
    // Check that step s is legal
    assert(steps.is_used(s));

    // Check that R2 is legal
    assert(regs.is_used(R2));

    // Don't override an *existing* call
    assert(steps[s].call == 0);

    assert(not expression_at(R2).is_index_var());

    // Set the call
    steps[s].call = R2;
}

void reg_heap::clear_call(int s)
{
    steps.access_unused(s).call = 0;
}

void reg_heap::clear_call_for_reg(int R)
{
    int s = step_index_for_reg(R);
    if (s > 0)
        clear_call( s );
}

void reg_heap::set_C(int R, closure&& C)
{
    assert(C);
    assert(not C.exp.head().is_a<expression>());
    clear_C(R);

    regs.access(R).C = std::move(C);
#ifndef NDEBUG
    for(int r: closure_at(R).Env)
        assert(regs.is_valid_address(r));
#endif
}

void reg_heap::clear_C(int R)
{
    truncate(regs.access_unused(R).C);
}

void reg_heap::mark_reg_created_by_step(int r, int s)
{
    assert(r > 0);
    assert(s > 0);

    int index = steps[s].created_regs.size();
    steps[s].created_regs.push_back(r);
    assert(regs.access(r).created_by.first == 0);
    assert(regs.access(r).created_by.second == 0);
    regs.access(r).created_by = {s,index};
}

int reg_heap::allocate()
{
    total_reg_allocations++;
    int r = regs.allocate();

    // invariant: newly allocated regs have no step //
    assert(not has_step(r));
    // invariant: newly allocated regs have no result //
    assert(not has_result(r));
    // invariant: newly allocated regs have no result //
    assert(not has_force(r));
    // invariant: newly allocated regs are type unknown //
    assert(regs[r].type == reg::type_t::unknown);
    // invariant: newly allocated regs have all unforced bits //

    return regs.allocate();
}

int reg_heap::allocate_reg_from_step(int s)
{
    int r = allocate();
    mark_reg_created_by_step(r,s);
    assert(not has_step(r));
    return r;
}

int reg_heap::allocate_reg_from_step(int s, closure&& C)
{
    int r = allocate_reg_from_step(s);
    set_C(r, std::move(C));
    return r;
}

int reg_heap::allocate_reg_from_step_in_token(int s, int t)
{
    int r = allocate_reg_from_step(s);
    tokens[t].vm_force.add_value(r, non_computed_index);
    tokens[t].vm_result.add_value(r, non_computed_index);
    tokens[t].vm_step.add_value(r, non_computed_index);
    return r;
}


// If we replace a computation at P that is newly defined in this token,
// there may be computations that call or use it that are also newly
// defined in this token.  Such computations must be cleared, because they
// do not use a value defined in a previous token, and so would not be detected
// as invalidate by invalidate_shared_regs( ), which can only detect computations
// as invalidate if they use a computation valid in a parent context.
//
// As a value, every computation that we invalidate is going to be newly defined
// in the current context.  Other computations can be invalidated later.

/// Update the value of a non-constant, non-computed index
void reg_heap::set_reg_value(int R, closure&& value, int t)
{
    total_set_reg_value++;
    assert(not is_dirty(t));
    assert(not children_of_token(t).size());
    assert(reg_is_changeable(R));

    if (not is_root_token(t) and tokens[t].version == tokens[parent_token(t)].version)
        tokens[t].version--;

    // assert(not is_root_token and tokens[t].version < tokens[parent_token(t)].version) 

    // Check that this reg is indeed settable
    if (not is_modifiable(expression_at(R)))
        throw myexception()<<"set_reg_value: reg "<<R<<" is not modifiable!";

    assert(not is_root_token(t));

    // Finally set the new value.
    int s = get_shared_step(R);

    assert(tokens[t].vm_step.empty());
    tokens[t].vm_step.add_value(R,s);

    assert(tokens[t].vm_result.empty());
    tokens[t].vm_result.add_value(R, non_computed_index);

    assert(tokens[t].vm_force.empty());
    tokens[t].vm_force.add_value(R, non_computed_index);

    assert(not children_of_token(t).size());

    // if the value is NULL, just leave the value and call both unset.
    //  (this could happen if we set a parameter value to null.)
    if (not value) return;

    // If the value is a pre-existing reg_var, then call it.
    if (value.exp.head().type() == index_var_type)
    {
        int Q = value.reg_for_index_var();

        // Set the call
        set_call_from_step(s, Q);
    }
    // Otherwise, regardless of whether the expression is WHNF or not, create a new reg for the value and call it.
    else
    {
        int R2 = allocate_reg_from_step_in_token(s,t);

        // Set the call
        set_C(R2, std::move( value ) );

        // clear 'reg created' edge from s to old call.
        set_call_from_step(s, R2);
    }

#if DEBUG_MACHINE >= 2
    check_used_regs();
    check_tokens();
#endif
}

void reg_heap::set_shared_value(int r, int v)
{
    // add a new computation
    int step = add_shared_step(r);
    add_shared_result(r, step);

    // set the value
    set_call(r, v);
}

/*
 * If parent token's version is greater than its child, this means that there could
 * be computations in the parent that are shared into the child that should not be.
 *
 * This occurs EITHER if we perform computation in the parent, OR of we alter a modifiable
 * value in the child.  Therefore, we increase the root version (mark_completely_dirty)
 * before executing in the root token, and decrease the child version when changing its 
 * modifiable values.
 *
 * Computations that are improperly shared into the child have dependencies on computations
 * in the parent context even though these computations are overridden in the child context.
 * We detect and invalidate such computations in invalidate_shared_regs( ).
 */

void reg_heap::mark_completely_dirty(int t)
{
    auto& version = tokens[t].version;
    for(int t2:tokens[t].children)
        version = std::max(version, tokens[t2].version+1);
    max_version = std::max(version, max_version);
}

bool reg_heap::is_dirty(int t) const
{
    for(int t2:tokens[t].children)
        if (tokens[t].version > tokens[t2].version)
            return true;
    return false;
}

// Note that a context can be completely dirty, w/o being dirty :-P
bool reg_heap::is_completely_dirty(int t) const
{
    for(int t2:tokens[t].children)
        if (tokens[t].version <= tokens[t2].version)
            return false;
    return true;
}
  
std::vector<int> reg_heap::used_regs_for_reg(int r) const
{
    vector<int> U;
    if (not has_step(r)) return U;

    for(const auto& [res,index]: step_for_reg(r).used_inputs)
        U.push_back(results[res].source_reg);

    return U;
}

std::vector<int> reg_heap::forced_regs_for_reg(int r) const
{
    vector<int> U;
    if (not has_step(r)) return U;

    for(int r: step_for_reg(r).forced_regs)
        U.push_back(r);

    return U;
}

void reg_heap::reclaim_used(int r)
{
    // Mark this reg as not used (but not free) so that we can stop worrying about upstream objects.
    assert(not regs.access(r).created_by.first);
    assert(not regs.access(r).created_by.second);
    assert(not has_step(r));
  
    regs.reclaim_used(r);
}

template <typename T>
void insert_at_end(vector<int>& v, const T& t)
{
    v.insert(v.end(), t.begin(), t.end());
}

void reg_heap::get_roots(vector<int>& scan, bool keep_identifiers) const
{
    insert_at_end(scan, stack); // inc_heads = yes
    insert_at_end(scan, temp); // yes
    insert_at_end(scan, heads); // yes

    // FIXME: We want to remove all of these.
    // * we should be able to remove random_variables_.  However, walking random_variables_ might find references to old, destroyed, variables then.
    insert_at_end(scan, likelihood_heads); // yes
    insert_at_end(scan, random_variables_); // yes
    insert_at_end(scan, transition_kernels_); // yes

    for(const auto& C: closure_stack)
        for(int r: C.Env)
            scan.push_back(r);

    for(const auto& [name,reg]: parameters) // yes
        scan.push_back(reg);
    if (keep_identifiers)
        for(const auto& [name,reg]: identifiers) // no
            scan.push_back(reg);
}

/// Add an expression that may be replaced by its reduced form
int reg_heap::add_compute_expression(const expression_ref& E)
{
    allocate_head(preprocess(E));

    return heads.size() - 1;
}

int reg_heap::add_named_head(const string& name, int r)
{
    int h = heads.size();
    heads.push_back(r);
    assert(not named_heads.count(name));
    named_heads[name] = h;
    return h;
}

optional<int> reg_heap::lookup_named_head(const string& name)
{
    auto it = named_heads.find(name);
    if (it == named_heads.end())
        return {};
    else
        return it->second;
}

int reg_heap::add_perform_io_head()
{
    perform_io_head = add_compute_expression(var("Compiler.IO.unsafePerformIO"));
    return *perform_io_head;
}

// 1. Pass in the program without logging state.
// 2. Generate the loggers regardless.
// 3. Return the value, and store it in the program head
// 4. Register the logging head, but don't return it.

int reg_heap::add_program(const expression_ref& E)
{
    if (program_result_head or logging_head)
        throw myexception()<<"Trying to set program a second time!";

    auto P = E;
    P = {var("Probability.Random.gen_model_no_alphabet"), P};
    P = {var("Compiler.IO.unsafePerformIO"), P};

    int program_head = add_compute_expression(P);
    P = reg_var(heads[program_head]);

    program_result_head = add_compute_expression({fst,P});

    expression_ref L = {var("Probability.Random.log_to_json"),{snd, P}};
    L = {var("Data.JSON.c_json"), L};

    logging_head = add_compute_expression(L);

    return *program_result_head;
}

void reg_heap::stack_push(int r)
{
    stack.push_back(r);
}

void reg_heap::stack_pop(int r)
{
    int r2 = stack_pop();
    if (r != r2)
        throw myexception()<<"Trying to pop reg "<<r<<" but got reg "<<r2<<"!";
}

int reg_heap::stack_pop()
{
    if (stack.empty())
        throw myexception()<<"Trying to pop an empty stack!";
    int r = stack.back();
    stack.pop_back();
    return r;
}

int reg_heap::set_head(int index, int R2)
{
    int R1 = heads[index];

    heads[index] = R2;

    return R1;
}

int reg_heap::set_head(int index, closure&& C)
{
    int R = allocate();

    set_head(index, R);

    set_C(R, std::move(C) );

    return R;
}

int reg_heap::allocate_head(closure&& C)
{
    int R = allocate();

    heads.push_back(R);

    set_C(R, std::move(C));

    return R;
}

int reg_heap::push_temp_head()
{
    int R = allocate();

    temp.push_back(R);

    return R;
}

int reg_heap::push_temp_head(closure&& C)
{
    int R = push_temp_head();

    set_C(R, std::move(C));

    return R;
}

void reg_heap::pop_temp_head()
{
//    int R = temp.back();

    temp.pop_back();
}

void reg_heap::resize(int s)
{
    assert(regs.size() == s);

    auto old_size = prog_steps.size();
    // Extend program.  Use regs.size() instead of size()
    prog_steps.resize(regs.size());
    prog_results.resize(regs.size());
    prog_force.resize(regs.size());
    prog_temp.resize(regs.size());

    // Now we can use size() again.
    for(auto i=old_size;i<size();i++)
    {
        prog_steps[i] = non_computed_index;
        prog_results[i] = non_computed_index;
        prog_force[i] = non_computed_index;

        assert(prog_steps[i] == non_computed_index);
        assert(prog_results[i] == non_computed_index);
        assert(prog_force[i] == non_computed_index);
        assert(prog_temp[i].none());
    }
}

bool reg_heap::reg_is_constant(int r) const
{
    return regs.access(r).type == reg::type_t::constant;
}

bool reg_heap::reg_is_changeable(int r) const
{
    return regs.access(r).type == reg::type_t::changeable;
}

void reg_heap::make_reg_changeable(int r)
{
    assert( regs.access(r).type == reg::type_t::changeable or regs.access(r).type == reg::type_t::unknown );

    regs.access(r).type = reg::type_t::changeable;
}

bool reg_heap::result_is_called_by(int res1, int res2) const
{
    for(int res: results[res2].called_by)
        if (res == res1)
            return true;

    return false;
}

bool reg_heap::result_is_used_by(int s1, int res2) const
{
    for(auto& [s,index]: results[res2].used_by)
        if (s == s1)
            return true;

    return false;
}

bool reg_heap::force_is_forced_by(int f1, int f2) const
{
    for(auto& [f,index]: forces[f2].forced_by)
        if (f == f1)
            return true;

    return false;
}

bool reg_heap::reg_is_used_by(int r1, int r2) const
{
    int s1 = step_index_for_reg(r1);
    int res2 = result_index_for_reg(r2);

    return result_is_used_by(s1,res2);
}

bool reg_heap::reg_is_forced_by2(int r1, int r2) const
{
    int f1 = force_index_for_reg(r1);
    int f2 = force_index_for_reg(r2);

    return force_is_forced_by(f1,f2);
}

void reg_heap::check_tokens() const
{
#ifndef NDEBUG
    for(int c=0;c<get_n_contexts();c++)
    {
        int t = token_for_context(c);
        if (t >= 0)
        {
            assert(tokens[t].is_referenced());
            assert(tokens[t].used);
        }
    }

    for(int t=0;t<tokens.size();t++)
        if (token_is_used(t))
        {
            assert(tokens[t].is_referenced() or tokens[t].children.size() >= 1);
            for(int t2: children_of_token(t))
                assert(tokens[t].version >= tokens[t2].version);
        }
#endif
}

void reg_heap::check_used_regs_in_token(int t) const
{
    assert(token_is_used(t));

    constexpr int force_bit = 2;
    constexpr int result_bit = 0;
    constexpr int step_bit = 1;

    for(auto [r,f]: tokens[t].delta_force())
    {
        assert(not prog_temp[r].test(force_bit));
        prog_temp[r].set(force_bit);

        // No results for constant regs
        if (f > 0)
            assert(not reg_is_constant(r));
    }

    for(auto [r,res]: tokens[t].delta_result())
    {
        assert(not prog_temp[r].test(result_bit));
        prog_temp[r].set(result_bit);

        // No results for constant regs
        if (res > 0)
        {
            assert(not reg_is_constant(r));
            assert(not steps.is_free(results[res].source_step));
            int call = results[res].call_edge.first;
            if (call > 0)
                assert(not results.is_free(call));
        }
    }
    for(auto [r,step]: tokens[t].delta_step())
    {
        assert(not prog_temp[r].test(step_bit));
        prog_temp[r].set(step_bit);

        // If the step is unshared, the result must be unshared as well: this allows us to just walk unshared results.
        assert(prog_temp[r].test(result_bit) and prog_temp[r].test(step_bit));
        // No steps for constant regs
        if (step > 0)
            assert(not reg_is_constant(r));
    }

    // FIXME - nonlocal. The same result/step are not set in multiple places!

    for(auto [r,step]: tokens[t].delta_step())
    {
        if (step < 0) continue;
        
        for(const auto& [res2,index2]: steps[step].used_inputs)
        {
            // Used regs should have back-references to R
            assert( result_is_used_by(step, res2) );

            // Used computations should be mapped computation for the current token, if we are at the root
            int R2 = results[res2].source_reg;
            assert(reg_is_changeable(R2));

            // The used result should be referenced somewhere more root-ward
            // so that this result can be invalidated, and the used result won't be GC-ed.
            // FIXME - nonlocal.  assert(is_modifiable(expression_at(R2)) or result_is_referenced(t,res2));
      
            // Used results should have values
            assert(results[res2].value);
        }
    }

    for(auto [reg, res]: tokens[t].delta_result())
    {
        if (res < 0) continue;

        int step = results[res].source_step;
        int call = steps[step].call;
        
        assert(steps[step].source_reg == reg);
        //FIXME! Check that source_step is in same token, for same reg
        int value = results[res].value;

        if (results[res].flags.test(result_bit))
            assert(is_root_token(t));

        if (results[res].flags.test(step_bit))
            assert(is_root_token(t));

        if (value)
            assert(call);

        if (call and value == call)
            assert(reg_is_constant(call));

        if (call and value and reg_is_constant(call))
            assert(value == call);

        if (t != root_token) continue;

        // Regs with values should have back-references from their call.
        if (value and not reg_is_constant(call))
        {
            assert( has_result(call) );
            int res2 = result_index_for_reg(call);
            assert( result_is_called_by(res, res2) );
        }

        // If we have a value, then our call should have a value
        if (value)
            assert(reg_has_value(call));
    }

    assert(tokens[t].delta_result().size() <= tokens[t].delta_force().size());

    assert(tokens[t].delta_step().size() <= tokens[t].delta_result().size());

    for(auto [reg,f]: tokens[t].delta_force())
    {
        prog_temp[reg].reset(result_bit);
        prog_temp[reg].reset(step_bit);
        prog_temp[reg].reset(force_bit);
    }

    for(auto [reg,res]: tokens[t].delta_result())
    {
        prog_temp[reg].reset(result_bit);
        prog_temp[reg].reset(step_bit);
        prog_temp[reg].reset(force_bit);
    }

    for(auto [reg,step]: tokens[t].delta_step())
    {
        prog_temp[reg].reset(result_bit);
        prog_temp[reg].reset(step_bit);
        prog_temp[reg].reset(force_bit);
    }
}

void reg_heap::check_used_regs() const
{
    assert(tokens[root_token].vm_step.empty());
    assert(tokens[root_token].vm_result.empty());

    for(int t=0; t< tokens.size(); t++)
        if (token_is_used(t))
            check_used_regs_in_token(t);

    for(auto& S:steps)
    {
        if (S.call > 0)
            assert(not regs.is_free(S.call));
    }

    // Check results that are not mapped
    for(const auto& result: results)
    {
        assert(not steps.is_free(result.source_step));
        int call = result.call_edge.first;
        if (call > 0)
            assert(not results.is_free(call));
    }

    for(int r=regs.n_null();r<regs.size();r++)
        if (regs.is_used(r) and reg_is_changeable(r))
        {
            // If we have no step, then we should have no result, and be completely unforced.
            if (not has_step(r))
            {
                assert(prog_force[r] == non_computed_index);
                assert(not has_result(r));
                assert(not has_force(r));
                continue;
            }

            if (not has_result(r))
            {
                assert(not has_force(r));
            }

            if (has_force(r))
            {
                for(auto r2: step_for_reg(r).forced_regs)
                    assert(has_force(r2));
                for(auto r2: used_regs_for_reg(r))
                    assert(has_force(r2));
                if (reg_is_changeable(step_for_reg(r).call))
                    assert(has_force(step_for_reg(r).call));
            }
        }
}

int reg_heap::get_shared_step(int r)
{
    // 1. Get a new computation
    int s = steps.allocate();
    total_step_allocations++;
  
    // 2. Set the source of the computation
    steps[s].source_reg = r;

    assert(s > 0);
    
    return s;
}

/// Add a shared step at (t,r) -- assuming there isn't one already
int reg_heap::add_shared_step(int r)
{
    assert(not has_step(r));

    // Allocate a step
    int s = get_shared_step(r);

    // Link it in to the mapping
    prog_steps[r] = s;

    assert(s > 0);

    return s;
}

int reg_heap::get_shared_result(int r, int s)
{
    // 1. Get a new result
    int res = results.allocate();
    total_comp_allocations++;
  
    // 2. Set the source of the result
    results[res].source_step = s;
    results[res].source_reg = r;

    assert(res > 0);

    return res;
}

/// Add a shared result at (t,r) -- assuming there isn't one already
int reg_heap::add_shared_result(int r, int s)
{
    assert(not has_result(r));
    // There should already be a step, if there is a result
    assert(has_step(r));

    // Get a result
    int res = get_shared_result(r,s);

    // Link it in to the mapping
    prog_results[r] = res;

    assert(res > 0);

    return res;
}

int reg_heap::get_shared_force(int r, int s, int res)
{
    // 1. Get a new force
    int f = forces.allocate();
    total_comp_allocations++;

    // 2. Set the source of the force
    forces[f].source_step = s;
    forces[f].source_result = res;
    forces[f].source_reg = r;

    assert(f > 0);

    return f;
}

/// Add a shared force at (t,r) -- assuming there isn't one already
int reg_heap::add_shared_force(int r, int s, int res)
{
    assert(not has_force(r));
    // There should already be a step, if there is a force
    assert(has_step(r));
    assert(has_result(r));

    // Get a force
    int f = get_shared_force(r,s,res);

    // Link it in to the mapping
    prog_force[r] = f;

    assert(f > 0);

    return f;
}

void reg_heap::check_back_edges_cleared_for_step(int s)
{
    for(auto& [res,index]: steps.access_unused(s).used_inputs)
        assert(index == 0);
    for(auto& r: steps.access_unused(s).created_regs)
    {
        auto [step, index] = regs.access(r).created_by;
        assert(step == 0);
        assert(index == 0);
    }
}

void reg_heap::check_back_edges_cleared_for_result(int res)
{
    assert(results.access_unused(res).call_edge.second == 0);
}

void reg_heap::check_back_edges_cleared_for_force(int f)
{
    for(auto& [res,index]: forces.access_unused(f).forced_inputs)
        assert(index == 0);
}

void reg_heap::clear_back_edges_for_reg(int r)
{
    assert(r > 0);
    auto& created_by = regs.access(r).created_by;
    auto [s,j] = created_by;
    if (s > 0)
    {
        auto& backward = steps[s].created_regs;
        assert(0 <= j and j < backward.size());

        // Clear the forward edge.
        created_by = {0, 0};

        // Move the last element to the hole, and adjust index of correspond forward edge.
        if (j + 1 < backward.size())
        {
            backward[j] = backward.back();
            auto& forward2 = regs.access(backward[j]);
            forward2.created_by.second = j;

            assert(regs.access(backward[j]).created_by.second == j);
        }
        backward.pop_back();
    }
}

void reg_heap::clear_back_edges_for_step(int s)
{
    assert(s > 0);
    for(auto& forward: steps[s].used_inputs)
    {
        auto [res,j] = forward;
        auto& backward = results[res].used_by;
        assert(0 <= j and j < backward.size());

        forward = {0,0};

        if (j+1 < backward.size())
        {
            // erase the backward edge by moving another backward edge on top of it.
            backward[j] = backward.back();
            auto [s2,i2] = backward[j];
            // adjust the forward edge for that backward edge
            auto& forward2 = steps[s2].used_inputs;
            assert(0 <= i2 and i2 < forward2.size());
            forward2[i2].second = j;

            assert(steps[s2].used_inputs[i2].second == j);
            assert(results[forward2[i2].first].used_by[forward2[i2].second].second == i2);
        }

        backward.pop_back();
    }
    steps[s].used_inputs.clear();

    for(auto& r: steps[s].created_regs)
        regs.access(r).created_by = {0,{}};
    steps[s].created_regs.clear();
}

void reg_heap::clear_back_edges_for_result(int res)
{
    assert(res > 0);
    // FIXME! If there is a value, set, there should be a call_edge
    //        Should we unmap all values with no .. value/call_edge?
    auto [call,j] = results[res].call_edge;
    if (call)
    {
        assert(results[res].value);

        auto& backward = results[call].called_by;
        int j = results[res].call_edge.second;
        assert(0 <= j and j < backward.size());

        // Clear the forward edge.
        results[res].call_edge = {0, 0};

        // Move the last element to the hole, and adjust index of correspond forward edge.
        if (j+1 < backward.size())
        {
            backward[j] = backward.back();
            auto& forward2 = results[backward[j]];
            forward2.call_edge.second = j;

            assert(results[backward[j]].call_edge.second == j);
        }
        backward.pop_back();
    }
}

void reg_heap::clear_back_edges_for_force(int f)
{
    assert(f > 0);

    for(auto& forward: forces[f].forced_inputs)
    {
        auto [f3,j] = forward;
        auto& backward = forces[f3].forced_by;
        assert(0 <= j and j < backward.size());

        forward = {0,0};

        if (j+1 < backward.size())
        {
            // erase the backward edge by moving another backward edge on top of it.
            backward[j] = backward.back();
            auto [f2,i2] = backward[j];
            // adjust the forward edge for that backward edge
            auto& forward2 = forces[f2].forced_inputs;
            assert(0 <= i2 and i2 < forward2.size());
            forward2[i2].second = j;

            assert(forces[f2].forced_inputs[i2].second == j);
            assert(forces[forward2[i2].first].forced_by[forward2[i2].second].second == i2);
        }

        backward.pop_back();
    }
    forces[f].forced_inputs.clear();

    // If this Force is destroyed, the Force that forced it will NOT survive,
    // Therefore, we DON'T need to clear these edges.
    for(auto& backward: forces[f].forced_by)
    {
        auto [f3,j] = backward;
        auto& forward = forces[f3].forced_inputs;
        assert(0 <= j and j < forward.size());

        // WHY?
        backward = {0,0};

        if (j+1 < forward.size())
        {
            //erase the forward edge by moving another forward edge on top of it.
            forward[j] = forward.back();
            auto [f2,i2] = forward[j];
            // adjust the forward edge for that backward edge
            auto& backward2 = forces[f2].forced_by;
            assert( 0 <= i2 and i2 <= backward2.size());
            backward2[i2].second = j;

            assert(backward2[i2].first == f3);
            assert(forces[f2].forced_by[i2].second == j);
            assert(forces[backward2[i2].first].forced_inputs[backward2[i2].second].second == i2);
        }
        forward.pop_back();
    }
    forces[f].forced_by.clear(); // OK?
}

void reg_heap::clear_step(int r)
{
    assert(not has_force(r));
    assert(not has_result(r));
    int s = prog_steps[r];
    prog_steps[r] = non_computed_index;
  
    if (s > 0)
    {
#ifndef NDEBUG
        check_back_edges_cleared_for_step(s);
#endif
        steps.reclaim_used(s);
    }
}

void reg_heap::clear_result(int r)
{
    assert(not has_force(r));

    int res = prog_results[r];
    prog_results[r] = non_computed_index;

    if (res > 0)
    {
#ifndef NDEBUG
        check_back_edges_cleared_for_result(res);
#endif
        results.reclaim_used(res);
    }
}

void reg_heap::clear_force(int r)
{
    int f = prog_force[r];
    prog_force[r] = non_computed_index;

    if (f > 0)
    {
#ifndef NDEBUG
        check_back_edges_cleared_for_force(f);
#endif
        forces.reclaim_used(f);
    }
}

const expression_ref& reg_heap::get_parameter_value_in_context(int p, int c)
{
    int& R = parameters[p].second;

    return get_reg_value_in_context(R, c);
}

const expression_ref& reg_heap::get_reg_value_in_context(int& R, int c)
{
    total_get_reg_value++;
    if (regs.access(R).type == reg::type_t::constant) return expression_at(R);

    total_get_reg_value_non_const++;
    reroot_at_context(c);

    if (has_result(R))
    {
        total_get_reg_value_non_const_with_result++;
        int R2 = result_value_for_reg(R);
        if (R2) return expression_at(R2);
    }

    // If the value needs to be computed (e.g. its a call expression) then compute it.
    auto [R2, value] = incremental_evaluate_in_context(R,c,true);
    R = R2;

    return expression_at(value);
}

void reg_heap::set_reg_value_in_context(int P, closure&& C, int c)
{
    int t = switch_to_child_token(c);

    set_reg_value(P, std::move(C), t);
}

pair<int,int> reg_heap::incremental_evaluate_in_context(int R, int c, bool force)
{
#if DEBUG_MACHINE >= 2
    check_used_regs();
#endif

    reroot_at_context(c);
    mark_completely_dirty(root_token);
    auto p = incremental_evaluate(R, force);

#if DEBUG_MACHINE >= 2
    check_used_regs();
#endif

    return p;
}

const closure& reg_heap::lazy_evaluate(int& R, bool force)
{
    mark_completely_dirty(root_token);
    auto [R2, value] = incremental_evaluate(R, force);
    R = R2;
    return closure_at(value);
}

const closure& reg_heap::lazy_evaluate(int& R, int c, bool force)
{
    auto [R2, value] = incremental_evaluate_in_context(R,c,force);
    R = R2;
    return closure_at(value);
}

const closure& reg_heap::lazy_evaluate_head(int index, int c, bool force)
{
    int R1 = heads[index];
    auto [R2, value] = incremental_evaluate_in_context(R1,c,force);
    if (R2 != R1)
        set_head(index, R2);

    return closure_at(value);
}

const closure& reg_heap::lazy_evaluate_unchangeable(int& R)
{
    R = incremental_evaluate_unchangeable(R);
    return closure_at(R);
}

int reg_heap::get_modifiable_value_in_context(int R, int c)
{
    assert( is_modifiable(expression_at(R)) );
    assert( reg_is_changeable(R) );

    reroot_at_context(c);

    return call_for_reg(R);
}

int reg_heap::add_identifier(const string& name)
{
    // if there's already an 's', then complain
    if (identifiers.count(name))
        throw myexception()<<"Cannot add identifier '"<<name<<"': there is already an identifier with that name.";

    int R = allocate();

    identifiers[name] = R;
    return R;
}

reg_heap::reg_heap(const std::shared_ptr<module_loader>& L)
    :regs(1,[this](int s){resize(s);}, [this](){collect_garbage();} ),
     steps(1),
     results(1),
     forces(1),
     P(new Program(L)),
     prog_steps(1, non_computed_index),
     prog_results(1, non_computed_index),
     prog_force(1, non_computed_index),
     prog_temp(1)
{
}

void reg_heap::release_scratch_list() const
{
    n_active_scratch_lists--;
}

vector<int>& reg_heap::get_scratch_list() const
{
    while(n_active_scratch_lists >= scratch_lists.size())
        scratch_lists.push_back( new Vector<int> );

    vector<int>& v = *scratch_lists[ n_active_scratch_lists++ ];

    v.clear();

    return v;
}

optional<int> reg_heap::maybe_find_parameter(const string& s) const
{
    for(int i=0;i<parameters.size();i++)
        if (parameters[i].first == s)
            return i;

    return {};
}

int reg_heap::find_parameter(const string& s) const
{
    auto index = maybe_find_parameter(s);
    if (not index)
        throw myexception()<<"Can't find parameter '"<<s<<"'!";
    return *index;
}

const vector<int>& reg_heap::transition_kernels() const
{
    return transition_kernels_;
}

int reg_heap::add_transition_kernel(int r)
{
    int i = transition_kernels_.size();
    transition_kernels_.push_back(r);
    return i;
}

int reg_heap::add_modifiable_parameter(const string& full_name)
{
    return add_parameter(full_name, modifiable());
}

int reg_heap::add_parameter(const string& full_name, const expression_ref& E)
{
    // 1. Allocate space for the expression
    int r = allocate();
    set_C(r, preprocess( E ) );

    add_parameter(full_name, r);

    return r;
}

void reg_heap::add_parameter(const string& full_name, int r)
{
    assert(full_name.size() != 0);

    // 1. Check that we don't already have a parameter with that name
    for(const auto& [p_name, _]: parameters)
        if (p_name == full_name)
            throw myexception()<<"A parameter with name '"<<full_name<<"' already exists - cannot add another one.";

    // 2. Allocate space for the parameter
    parameters.push_back( {full_name, r} );
}
