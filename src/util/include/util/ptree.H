#ifndef PTREE_H
#define PTREE_H

#include <iostream>
#include <vector>
#include <utility>
#include <vector>

#include <boost/variant.hpp>
#include <optional>

#include "util/json.hh"

struct monostate{};
std::ostream& operator<<(std::ostream& o,const monostate&);
inline bool operator<(const monostate&, const monostate&) {return false;}
inline bool operator>(const monostate&, const monostate&) {return false;}
inline bool operator==(const monostate&, const monostate&) {return true;}
inline bool operator!=(const monostate&, const monostate&) {return false;}

struct ptree;
struct ptree: public std::vector<std::pair<std::string,ptree>>
{
public:
    typedef boost::variant<monostate,std::string,int,double,bool> value_t;
    value_t value;

    bool value_is_empty() const;

    bool is_null() const;

    template <typename T>     bool has_value() const {return false;}
    template <typename T>     bool is_a() const {return empty() and has_value<T>();}

    template <typename T>       T& get_value()       {return boost::get<T>(value);}
    template <typename T> const T& get_value() const {return boost::get<T>(value);}

    template <typename T> void put_value(const T& t) {value = t;}
    void put_value(const char* s) {put_value<std::string>(s);}

    template <typename T>       T& get(const std::string& key)       {return get_child(key).get_value<T>();}
    template <typename T> const T& get(const std::string& key) const {return get_child(key).get_value<T>();}

    void erase(const std::string& key);

    std::optional<int> get_child_index(const std::string& key) const;

    int count(const std::string& key) const;

    bool operator==(const ptree&) const;
    bool operator!=(const ptree&) const;

    bool operator==(int i) const
    {
	return (is_a<int>() and get_value<int>() == i);
    }

    bool operator==(double d) const
    {
	return (is_a<double>() and get_value<double>() == d);
    }

    bool operator==(bool b) const
    {
	return (is_a<bool>() and get_value<bool>() == b);
    }

    bool operator==(const std::string& s) const
    {
	return (is_a<std::string>() and get_value<std::string>() == s);
    }

    bool operator==(const char* s) const
    {
	return (*this) == std::string(s);
    }

          ptree* get_child_optional(const std::string& key);
    const ptree* get_child_optional(const std::string& key) const;

    ptree& get_child(const std::string& key);
    const ptree& get_child(const std::string& key) const;

          ptree* get_path_optional(const std::vector<std::string>& path, int i=0);
    const ptree* get_path_optional(const std::vector<std::string>& path, int i=0) const;

    ptree& get_path(const std::vector<std::string>& path, int i=0);
    const ptree& get_path(const std::vector<std::string>& path, int i=0) const;

    int make_index(const std::string& key);
    ptree& make_child(const std::string& key);
    ptree& make_path(const std::vector<std::string>& path, int i=0);
    
    template <typename T>
    T get(const std::string& key, const T& def) const
    {
	if (auto c = get_child_optional(key))
	    return c->get_value<T>();
	else
	    return def;
    }

    template <typename T>
    std::optional<T> get_optional(const std::string& key) const
    {
	if (auto c = get_child_optional(key))
	    return c->get_value<T>();
	else
	    return {};
    }


    operator bool () const;
    operator int () const;
    operator double () const;
    operator const std::string& () const;

    std::string show() const;

    ptree() {};

    ptree(const value_t& v):value(v) {};
    ptree(const char* s):value(std::string(s)) {};

    ptree(const std::vector<std::pair<std::string,ptree>>& x):std::vector<std::pair<std::string,ptree>>(x) { }

    ptree(const char* s,const std::vector<std::pair<std::string,ptree>>& x):std::vector<std::pair<std::string,ptree>>(x),value(std::string(s)) { }
    ptree(const value_t& v,const std::vector<std::pair<std::string,ptree>>& x):std::vector<std::pair<std::string,ptree>>(x),value(v) { }
};

template <>  inline bool ptree::has_value<bool>()   const {return value.which() == 4;}
template <>  inline bool ptree::has_value<int>()    const {return value.which() == 2;}
template <>  inline bool ptree::has_value<double>() const {return value.which() == 3;}
template <>  inline bool ptree::has_value<std::string>() const {return value.which() == 1;}

std::string show(const ptree& pt, int depth=0);

void to_json(json& j, const ptree::value_t& p);
void to_json(json& j, const ptree& p);


#endif
