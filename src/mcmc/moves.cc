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

#include "sample.H"
#include "util/range.H"
#include "util/rng.H"
#include "util/log-level.H"
#include <algorithm>
#include "mcmc.H"
#include "proposals.H"
#include "dp/3way.H"
#include "util/permute.H"
#include "alignment/alignment-util.H"
#include "mcmc/logger.H"

using MCMC::MoveStats;

using std::valarray;
using std::vector;
using std::string;
using std::map;

void slide_node_move(owned_ptr<Model>& P, MoveStats& Stats,int b) 
{
    slide_node(P,Stats,b);
}

void change_branch_length_move(owned_ptr<Model>& P, MoveStats& Stats,int b) 
{
    change_branch_length(P,Stats,b);
}

void change_branch_length_multi_move(owned_ptr<Model>& P, MoveStats& Stats,int b) 
{
    change_branch_length_multi(P,Stats,b);
}

void sample_tri_one(owned_ptr<Model>& P, MoveStats&,int b) 
{
    Parameters* PP = P.as<Parameters>();
    auto t = PP->t();

    int node1 = t.target(t.undirected(b));
    int node2 = t.source(t.undirected(b));
  
    if (uniform() < 0.5)
        std::swap(node1,node2);

    if (node1 < t.n_leaves())
        std::swap(node1,node2);
    
    tri_sample_alignment(*PP,node1,node2);
}

void sample_tri_branch_one(owned_ptr<Model>& P, MoveStats& Stats,int b) 
{
    Parameters* PP = P.as<Parameters>();

    MCMC::Result result(2);

    assert(PP->variable_alignment()); 

    auto t = PP->t();
  
    int node1 = t.target(t.undirected(b));
    int node2 = t.source(t.undirected(b));

    if (uniform() < 0.5)
        std::swap(node1,node2);

    if (node1 < t.n_leaves())
        std::swap(node1,node2);
    
    const double sigma = 0.3/2;
    double length1 = t.branch_length(b);
    double length2 = length1 + gaussian(0,sigma);
    if (length2 < 0) length2 = -length2;

    if (tri_sample_alignment_branch(*PP,node1,node2,b,1,length2)) {
        result.totals[0] = 1;
        result.totals[1] = std::abs(length2 - length1);
    }

    Stats.inc("sample_tri_branch",result);
}

void sample_cube_one(owned_ptr<Model>& P, MoveStats&,int b) 
{
    Parameters* PP = P.as<Parameters>();
    auto t = PP->t();

    int node1 = t.target(t.undirected(b));
    int node2 = t.source(t.undirected(b));
  
    if (uniform() < 0.5)
        std::swap(node1,node2);

    if (node1 < t.n_leaves())
        std::swap(node1,node2);
    
    cube_sample_alignment(*PP,node1,node2);
}

void sample_cube_branch_one(owned_ptr<Model>& P, MoveStats& Stats,int b) 
{
    Parameters* PP = P.as<Parameters>();

    MCMC::Result result(2);

    assert(PP->variable_alignment()); 

    auto t = PP->t();
  
    int node1 = t.target(t.undirected(b));
    int node2 = t.source(t.undirected(b));

    if (uniform() < 0.5)
        std::swap(node1,node2);

    if (node1 < t.n_leaves())
        std::swap(node1,node2);
    
    const double sigma = 0.3/2;
    double length1 = t.branch_length(b);
    double length2 = length1 + gaussian(0,sigma);
    if (length2 < 0) length2 = -length2;

    if (cube_sample_alignment_branch(*PP,node1,node2,b,1,length2)) {
        result.totals[0] = 1;
        result.totals[1] = std::abs(length2 - length1);
    }

    Stats.inc("sample_cube_branch",result);
}


void sample_parameter_and_alignment_on_branch(owned_ptr<Model>& P, MoveStats& Stats, int b, const Proposal& proposal)
{
    Parameters* PP = P.as<Parameters>();

    MCMC::Result result(1);

    assert(PP->variable_alignment()); 

    auto t = PP->t();

    int node1 = t.target(t.undirected(b));
    int node2 = t.source(t.undirected(b));

    if (uniform() < 0.5)
        std::swap(node1,node2);

    if (node1 < t.n_leaves())
        std::swap(node1,node2);
    
    if (tri_sample_alignment_and_parameter(*PP, node1, node2, proposal))
    {
        result.totals[0] = 1;
    }

    // FIXME... somehow the proposal itself has to know something about what to log.

    Stats.inc("sample_and_alignment_on_branch",result);
}


void sample_tri_branch_type_one(owned_ptr<Model>& P, MoveStats& Stats,int b) 
{
    Parameters* PP = P.as<Parameters>();

    MCMC::Result result(1);

    assert(PP->variable_alignment()); 

    auto t = PP->t();

    int node1 = t.target(t.undirected(b));
    int node2 = t.source(t.undirected(b));

    if (uniform() < 0.5)
        std::swap(node1,node2);

    if (node1 < t.n_leaves())
        std::swap(node1,node2);
    
    if (tri_sample_alignment_branch_model(*PP,node1,node2)) {
        result.totals[0] = 1;
    }

    Stats.inc("sample_tri_branch_type",result);
}


void sample_alignments_one(owned_ptr<Model>& P, MoveStats& Stats, int b)
{
    Parameters* PP = P.as<Parameters>();
    assert(PP->variable_alignment()); 

    if (uniform() < 0.01)
        alignment_slice_sample_branch_length(P, Stats, b);
    else
        sample_alignment(*PP,b);
}

void sample_node_move(owned_ptr<Model>& P, MoveStats&,int node) 
{
    Parameters* PP = P.as<Parameters>();
    assert(PP->variable_alignment()); 

    sample_node(*PP,node);
}

void sample_two_nodes_move(owned_ptr<Model>& P, MoveStats&,int n0) 
{
    Parameters* PP = P.as<Parameters>();
    assert(PP->variable_alignment()); 

    vector<int> nodes = A3::get_nodes_random(PP->t(),n0);
    int n1 = -1;
    for(int i=1;i<nodes.size();i++)
        if ((PP->t()).is_internal_node( nodes[i] )) {
            n1 = nodes[i];
            break;
        }
    assert(n1 != 1);

    int b = PP->t().undirected(PP->t().find_branch(n0,n1));

    sample_two_nodes(*PP,b);
}

// cost[b] should be the cost to visit all the branches after b.
// * the cost is the number of branches we need to move the LC.root across
// * we don't need to move the root across leaf branches -- we just have to
//   be on one of the endpoints of the branch
// * the cost of a non-leaf branch with branches_out={l,r} is 2*cost[l]+cost[r]
//   if we visit the subtree in front of l first.
vector<int> get_cost(const TreeInterface& t) {
    vector<int> cost(t.n_branches()*2,-1);
    vector<int> stack1; stack1.reserve(t.n_branches()*2);
    vector<int> stack2; stack2.reserve(t.n_branches()*2);
    for(int i=0;i<t.n_leaves();i++) {
        int b = t.reverse(i);
        cost[b] = 0;
        stack1.push_back(b);
    }
    
    while(not stack1.empty()) {
        // fill 'stack2' with branches before 'stack1'
        stack2.clear();
        for(int i=0;i<stack1.size();i++)
            t.append_branches_before(stack1[i], stack2);

        // clear 'stack1'
        stack1.clear();

        for(int i=0;i<stack2.size();i++) {
            vector<int> children = t.branches_after(stack2[i]);

            assert(children.size() == 2);
            int cost_l = cost[children[0]];
            int cost_r = cost[children[1]];
            if (cost_l != -1 and cost_r != -1) {
                if (not t.is_leaf_branch(children[0])) cost_l++;

                if (not t.is_leaf_branch(children[1])) cost_r++;

                if (cost_l > cost_r)
                    std::swap(cost_l,cost_r);

                cost[stack2[i]] = 2*cost_l + cost_r;
                stack1.push_back(stack2[i]);
            }
        }
    }
  
    // check that all the costs have been calculated
    for(int i=0;i<cost.size();i++)
        assert(cost[i] != -1);

    return cost;
}

vector<int> get_distance(const TreeInterface& t, int n)
{
    vector<int> D(t.n_nodes(),-1);
    D[n] = 0;

    vector<int> branches = t.branches_out(n);
    int d = 0;
    int i = 0;
    while(i<branches.size())
    {
        d++;
        int j=branches.size();
        for(;i<j;i++)
        {
            D[t.target(branches[i])] = d;
            t.append_branches_after(branches[i], branches);
        }
    }
    return D;
}

vector<int> walk_tree_path_toward_branch(const TreeInterface& t, int b0)
{
    vector<int> branches;
    for(auto b: t.branches_before(b0))
    {
	auto x = walk_tree_path_toward_branch(t, b);
	branches.insert(branches.end(), x.begin(), x.end());
    }
    // Put the incoming branches in last and TOGETHER
    for(auto b: t.branches_before(b0))
	branches.push_back(b);
    return branches;
}

vector<int> walk_tree_path_toward(const TreeInterface& t, int root)
{
    vector<int> branches;
    for(auto b: t.branches_in(root))
    {
	auto x = walk_tree_path_toward_branch(t, b);
	branches.insert(branches.end(), x.begin(), x.end());
    }
    // Put the incoming branches in last and TOGETHER
    for(auto b: t.branches_in(root))
	branches.push_back(b);
    return branches;
}

vector<int> reverse_branch_path(const TreeInterface& t, const vector<int>& branches)
{
    auto branches2 = branches;
    for(auto& b: branches2)
	b = t.reverse(b);
    std::reverse(branches2);
    return branches2;
}

vector<int> walk_tree_path_away(const TreeInterface& t, int node)
{
    return reverse_branch_path(t, walk_tree_path_toward(t, node));
}

vector<int> walk_tree_path_toward_and_away(const TreeInterface& t, int node)
{
    // Branches toward the node
    auto branches = walk_tree_path_toward(t, node);

    vector<int> branches2 = reverse_branch_path(t, branches);

    branches.insert(branches.end(), branches2.begin(), branches2.end());
    return branches;
}


vector<int> walk_tree_path(const TreeInterface& t, int root)
{
    vector<int> cost = get_cost(t);

    vector<int> D = get_distance(t,root);
    vector<int> tcost = cost;
    for(int i=0;i<cost.size();i++)
        tcost[i] += D[t.target(i)];

    vector<int> b_stack;
    b_stack.reserve(t.n_branches());
    vector<int> branches;
    branches.reserve(t.n_branches());
    vector<int> children;
    children.reserve(3);

    // get a leaf with minimum 'tcost'
    int leaf = 0;
    leaf = myrandom(t.n_leaves());
    for(int b=0;b<t.n_leaves();b++)
        if (tcost[b] < tcost[leaf])
            leaf = b;

    assert(t.source(leaf) == leaf);
    b_stack.push_back(leaf);

    while(not b_stack.empty()) {
        // pop stack into list
        branches.push_back(b_stack.back());
        b_stack.pop_back();

        // get children of the result
        children = t.branches_after(branches.back());
        sort(children.begin(), children.end());
        random_shuffle(children);

        // sort children in decrease order of cost
        if (children.size() < 2)
            ;
        else {
            if (children.size() == 2) {
                if (cost[children[0]] < cost[children[1]])
                    std::swap(children[0],children[1]);
            }
            else
                std::abort();
        }
      
        // put children onto the stack
        for(int b: children)
            b_stack.push_back(b);
    }

    assert(branches.size() == t.n_branches());

    vector<int> branches2(branches.size());
    for(int i=0;i<branches.size();i++)
        branches2[i] = t.undirected(branches[i]);

    return branches2;
}

void sample_branch_length_(owned_ptr<Model>& P,  MoveStats& Stats, int b)
{
    //std::clog<<"Processing branch "<<b<<" with root "<<P.subst_root()<<endl;

    double slice_fraction = P->load_value("branch_slice_fraction",0.9);

    bool do_slice = (uniform() < slice_fraction);
    if (do_slice)
        slice_sample_branch_length(P,Stats,b);
    else
        change_branch_length(P,Stats,b);
    
    // Find a random direction of this branch, conditional on pointing to an internal node.
    const auto t = P.as<Parameters>()->t();
    auto e = t.edge(b);
    if (uniform() < 0.5)
        e = e.reverse();

    if (t.is_leaf_node(e.node2))
        e = e.reverse();

    // NOTE! This pointer might be invalidated after the tree is changed by MH!
    //       We would modify T2 and then do T=T2, thus using the copied structue and destroying the original.

    // FIXME - this might move the accumulator off of the current branch (?)
    // TEST and Check Scaling of # of branches peeled
    if (t.n_nodes() > 2)
    {
        if (uniform() < 0.5)
            slide_node(P, Stats, t.find_branch(e));
        else 
            change_3_branch_lengths(P,Stats, e.node2);
    }

    if (not do_slice) {
        change_branch_length(P,Stats,b);
        change_branch_length(P,Stats,b);
    }
}

void walk_tree_sample_NNI_and_branch_lengths(owned_ptr<Model>& P, MoveStats& Stats) 
{
    Parameters& PP = *P.as<Parameters>();
    vector<int> branches = walk_tree_path(PP.t(), PP[0].subst_root());

    for(int i=0;i<branches.size();i++)
    {
        int b = branches[i];

        double U = uniform();

        if (U < 0.1)
            slice_sample_branch_length(P,Stats,b);

        if (PP.t().is_internal_branch(b)) 
        {
            // In theory the 3-way move should have twice the acceptance rate, when the branch length
            // is non-zero, and one of the two other topologies is good while one is bad.
            //
            // This seems to actually occur for the Enolase-48 data set.
            if (uniform() < 0.95)
                three_way_topology_sample(P,Stats,b);
            else
                two_way_NNI_sample(P,Stats,b);
        }

        if (U > 0.9)
            slice_sample_branch_length(P,Stats,b);
    }
}


void walk_tree_sample_NNI(owned_ptr<Model>& P, MoveStats& Stats)
{
    Parameters& PP = *P.as<Parameters>();
    vector<int> branches = walk_tree_path(PP.t(), PP[0].subst_root());

    for(int i=0;i<branches.size();i++) 
    {
        int b = branches[i];
        if (uniform() < 0.95)
            three_way_topology_sample(P,Stats,b);
        else
            two_way_NNI_sample(P,Stats,b);
    }
}


void walk_tree_sample_NNI_and_A(owned_ptr<Model>& P, MoveStats& Stats) 
{
    double NNI_A_fraction = P->load_value("NNI+A_fraction",0.01);

    Parameters& PP = *P.as<Parameters>();
    vector<int> branches = walk_tree_path(PP.t(), PP[0].subst_root());

    for(int i=0;i<branches.size();i++) 
    {
        int b = branches[i];
        if (uniform() < NNI_A_fraction)
            three_way_topology_and_alignment_sample(P,Stats,b);
        else
            if (uniform() < 0.95)
                three_way_topology_sample(P,Stats,b);
            else
                two_way_NNI_sample(P,Stats,b);
    }
}


void walk_tree_sample_alignments(owned_ptr<Model>& P, MoveStats& Stats) 
{
    Parameters& PP = *P.as<Parameters>();
    vector<int> branches = walk_tree_path(PP.t(), PP[0].subst_root());

    double cube_fraction = P->load_value("cube_fraction",0.00);

    for(int i=0;i<branches.size();i++) 
    {
        int b = branches[i];

        //    std::clog<<"Processing branch "<<b<<" with root "<<P.subst_root()<<endl;

        if ((uniform() < 0.15) and (PP.t().n_leaves() >2))
        {
            // FIXME: don't call sample_parameter_and_alignment_on_branch( ): something is wrong.
            if (uniform() < 0.5 or true)
            {
                if (uniform() < cube_fraction)
                    sample_cube_one(P,Stats,b);
                else
                    sample_tri_one(P,Stats,b);
            }
            else
            {
                // sample_parameter_and_alignment_on_branch(P,Stats,b);
            }
        }
        else
            sample_alignments_one(P,Stats,b);
    }
}


// FIXME: Realign from tips basically fails because the distance between sequences is too small!
void realign_from_tips(owned_ptr<Model>& P, MoveStats& Stats) 
{
    int AL0 = MCMC::alignment_length(*P.as<Parameters>());

    if (log_verbose>=3) std::cerr<<"realign_from_tips: |A0| = "<<AL0<<"\n";
    Parameters& PP = *P.as<Parameters>();
    auto t = PP.t();
    int toward_node = uniform(t.n_nodes()>2?t.n_leaves():0, t.n_nodes()-1);
    vector<int> branches = walk_tree_path_toward_and_away(t, toward_node);

    for(int b: branches)
    {
        sample_branch_length_(P,Stats,b);
        auto t = P.as<Parameters>()->t();
        int node1 = t.source(b);
        int node2 = t.target(b);
        if (log_verbose >=4)
        {
            auto length = [&](int node) {return (*P.as<Parameters>())[0].seqlength(node);};
            vector<int> children = t.branches_after(b);

            std::cerr<<"realign_from_tips:   realigning branch "<<b<<"\n";
            std::cerr<<"realign_from_tips:   orig branch lengths = "<<t.branch_length(b);
            if (children.size())
            {
                assert(children.size() == 2);
                std::cerr<<" -> ("<<t.branch_length(children[0])<<","<<t.branch_length(children[1])<<")";
            }
            std::cerr<<"\n";
            std::cerr<<"realign_from_tips:   orig seq lengths = "<<length(node1)<<" -> "<<length(node2);
            if (children.size())
            {
                assert(children.size() == 2);
                int node3 = t.target(children[0]);
                int node4 = t.target(children[1]);
                std::cerr<<" -> ("<<length(node3)<<","<<length(node4)<<")";
            }
            std::cerr<<"\n";
        }
        if (node2 >= t.n_leaves())
        {
            if (log_verbose >=3) std::cerr<<"     Performing 3-way alignment\n";
            tri_sample_alignment(*P.as<Parameters>(), node2, node1);
        }
        else
        {
            if (log_verbose >=3) std::cerr<<"     Performing 2-way alignment\n";
            sample_alignment(*P.as<Parameters>(), b);
        }
        sample_branch_length_(P,Stats,b);

        if (log_verbose >= 4)
        {
            auto t = P.as<Parameters>()->t();
            int node1 = t.source(b);
            int node2 = t.target(b);

            auto length = [&](int node) {return (*P.as<Parameters>())[0].seqlength(node);};
            std::cerr<<"realign_from_tips:   post seq lengths = "<<length(node1)<<" -> "<<length(node2);
            vector<int> children = t.branches_after(b);
            if (children.size())
            {
                assert(children.size() == 2);
                int node3 = t.target(children[0]);
                int node4 = t.target(children[1]);
                std::cerr<<" -> ("<<length(node3)<<","<<length(node4)<<")";
            }
            std::cerr<<"\n\n";
        }

        three_way_topology_sample(P,Stats,b);
    }
    int AL1 = MCMC::alignment_length(*P.as<Parameters>());
    if (log_verbose>=4) std::cerr<<"realign_from_tips: |A1| = "<<AL1<<"\n";

    MCMC::Result result(2);
    result.totals[0] = 1;
    result.totals[1] = std::abs(AL1-AL0);
    Stats.inc("realign_from_tips",result);
}

void walk_tree_sample_branch_lengths(owned_ptr<Model>& P, MoveStats& Stats) 
{
    Parameters& PP = *P.as<Parameters>();
    vector<int> branches = walk_tree_path(PP.t(), PP[0].subst_root());

    for(int i=0;i<branches.size();i++) 
    {
        int b = branches[i];

        // Do a number of changes near branch @b
        sample_branch_length_(P,Stats,b);
    }
}
