#ifndef SMODEL_SETUP_H
#define SMODEL_SETUP_H
#include <set>                                      // for set
#include <string>                                   // for string
#include <utility>                                  // for pair
#include <vector>                                   // for vector
#include "computation/expression/expression_ref.H"  // for expression_ref
#include "sequence/alphabet.H"                      // for alphabet
#include "unification.H"                            // for term_t, less
#include "util/json.hh"                             // for json
#include "util/ptree.H"                             // for ptree
class Rules;

class pretty_model_t
{
public:
    ptree main;
    std::vector<std::string> term_names;
    std::vector<pretty_model_t> terms;

    std::string show(bool = true) const;
    std::string show_main(bool = true) const;
    std::string show_extracted() const;

    pretty_model_t(const ptree& m);
};

class model_t
{
    ptree description;
public:
    std::set<std::string> imports;
    ptree type;
    std::set<term_t> constraints;
    expression_ref expression;

    pretty_model_t pretty_model() const {return {description};};

    std::string show(bool = true) const;

    std::string show_pretty(bool = true) const;
    std::string show_main(bool = true) const;
    std::string show_extracted() const;

    model_t() = default;
    model_t(const ptree&,const std::set<std::string>&, const ptree&, const std::set<term_t>&, const expression_ref&);
};

void to_json(json& j, const pretty_model_t& m);

model_t get_model(const Rules& R, const std::string& type, const std::string& model, const std::vector<std::pair<std::string,std::string>>& = std::vector<std::pair<std::string,std::string>>());

std::string default_markov_model(const alphabet& a);
#endif
