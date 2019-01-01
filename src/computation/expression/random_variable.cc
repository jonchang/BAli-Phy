#include "random_variable.H"
#include "expression.H"

closure random_variable_op(OperationArgs& Args)
{
    int r_var =     Args.reg_for_slot(0);
//    int r_pdf =     Args.reg_for_slot(1);
//    int r_range =   Args.reg_for_slot(2);
//    int r_c_range = Args.reg_for_slot(3);
//    int r_rate =    Args.reg_for_slot(4);

    // The random_variable should evaluate to its first argument, but not be replaced by it.
    Args.make_changeable();

    // Return a reference to the new random variable.
    return {index_var(0),{r_var}};
}

random_variable::random_variable()
    :Operation(2, random_variable_op,"random_variable") {}

bool is_random_variable(const expression_ref& E)
{
    bool result = E.head().type() == random_variable_type;
    assert(result == E.head().is_a<random_variable>());
    return result;
}
