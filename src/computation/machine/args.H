#ifndef COMPUTATION_H
#define COMPUTATION_H

#include <optional>
#include "computation/object.H"
#include "computation/closure.H"

class expression_ref;
class reg_heap;
class effect;

class OperationArgs: public Object
{
protected:
    reg_heap& M;

    int n_allocated = 0;

public:
    virtual OperationArgs* clone() const = 0;

    reg_heap& memory() {return M;}

    const reg_heap& memory() const {return M;}

    virtual const closure& current_closure() const = 0;

    virtual bool evaluate_changeables() const = 0;

    virtual void make_changeable() = 0;

    int n_args() const;

    const expression_ref& reference(int slot) const;

    int reg_for_slot(int slot) const;

    virtual int evaluate_reg(int) = 0;

    virtual int evaluate_reg_force(int) = 0;

    int evaluate_slot_force(int);

    virtual int evaluate_reg_to_reg(int) = 0;
  
    int evaluate_slot_to_reg(int);

    virtual const closure& evaluate_reg_to_closure(int) = 0;

    virtual const closure& evaluate_reg_to_closure_(int) = 0;

    const closure& evaluate_slot_to_closure(int);

    const closure& evaluate_slot_to_closure_(int);

    const expression_ref& evaluate_reg_to_object(int);

    const expression_ref& evaluate_reg_to_object_(int);

    const expression_ref& evaluate_slot_to_object(int);

    const expression_ref& evaluate_slot_to_object_(int);

    const expression_ref& evaluate(int);

    const expression_ref& evaluate_(int);

    std::optional<int> find_random_variable_in_root_token(int r);
    std::optional<int> find_modifiable_in_context(int r, int c);

    void stack_push(int);

    void stack_pop(int);

    void stack_pop();

    virtual void set_effect(const effect&);

    int allocate(closure&&);

    virtual int allocate_reg();

    OperationArgs(reg_heap& m);

    virtual ~OperationArgs();
};

#endif
