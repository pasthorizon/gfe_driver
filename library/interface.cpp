/**
 * Copyright (C) 2019 Dean De Leo, email: dleo[at]cwi.nl
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "interface.hpp"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>

#include "common/error.hpp"

#include "baseline/adjacency_list.hpp"
#include "reader/reader.hpp"
#if defined(HAVE_STINGER)
#include "stinger/stinger.hpp"
#endif

using namespace std;

/*****************************************************************************
 *                                                                           *
 *  Debug                                                                    *
 *                                                                           *
 *****************************************************************************/
//#define DEBUG
extern mutex _log_mutex [[maybe_unused]];
#define COUT_DEBUG_FORCE(msg) { std::scoped_lock<std::mutex> lock{_log_mutex}; std::cout << "[Interface::" << __FUNCTION__ << "] " << msg << std::endl; }
#if defined(DEBUG)
    #define COUT_DEBUG(msg) COUT_DEBUG_FORCE(msg)
#else
    #define COUT_DEBUG(msg)
#endif


namespace library {
/*****************************************************************************
 *                                                                           *
 *  Factory                                                                  *
 *                                                                           *
 *****************************************************************************/
ImplementationManifest::ImplementationManifest(const string& name, const string& description, unique_ptr<Interface> (*factory)(bool)) :
    m_name(name), m_description(description), m_factory(factory){ }

std::unique_ptr<Interface> generate_baseline_adjlist(bool directed_graph){ // directed or undirected graph
    return unique_ptr<Interface>{ new AdjacencyList(directed_graph) };
}

#if defined(HAVE_STINGER)
std::unique_ptr<Interface> generate_stinger(bool directed_graph){
    return unique_ptr<Interface>{ new Stinger(directed_graph) };
}
#endif

vector<ImplementationManifest> implementations() {
    vector<ImplementationManifest> result;

    result.emplace_back("baseline", "Sequential baseline, based on adjacency list", &generate_baseline_adjlist);

#if defined(HAVE_STINGER)
    result.emplace_back("stinger", "Stinger library", &generate_stinger);
#endif

    return result;
}

/*****************************************************************************
 *                                                                           *
 *  Base interface                                                           *
 *                                                                           *
 *****************************************************************************/
Interface::Interface(){}
Interface::~Interface(){}
void Interface::on_main_init(int num_threads){ };
void Interface::on_thread_init(int thread_id){ };
void Interface::on_thread_destroy(int thread_id){ } ;
void Interface::on_main_destroy(){ };
bool Interface::has_edge(uint64_t source, uint64_t destination) const {
    return !isnan(get_weight(source, destination));
}
void Interface::dump() const{
    dump_ostream(std::cout);
}
void Interface::dump(const std::string& path) const {
    fstream handle(path.c_str());
    if(!handle.good()) ERROR("[dump] Cannot open the file: `" << path << "'");
    dump_ostream(handle);
    handle.close();
}

bool Interface::is_undirected() const {
    return !is_directed();
}


/*****************************************************************************
 *                                                                           *
 *  Update interface                                                         *
 *                                                                           *
 *****************************************************************************/
bool UpdateInterface::batch(const SingleUpdate* array, size_t array_sz, bool force){
    bool result = true;
    const SingleUpdate* __restrict A = array;
    COUT_DEBUG("batch: " << array_sz << ", force: " << force);

    if(force){
        // now a bit of a hack, we want all updates to succeed. An update may fail if a vertex is still being added
        // by another thread in the meanwhile
        for(uint64_t i = 0; i < array_sz; i++){
            if(A[i].m_weight >= 0){ // insert
                while ( ! add_edge(graph::WeightedEdge{A[i].m_source, A[i].m_destination, A[i].m_weight}) ) { /* nop */ };
            } else { // remove
//                bool res = remove_edge(graph::Edge{A[i].m_source, A[i].m_destination});
//                assert(res == true);
                while ( ! remove_edge(graph::Edge{A[i].m_source, A[i].m_destination}) ) { /* nop */ };
            }
        }
    } else {
        for(uint64_t i = 0; i < array_sz; i++){
            if(A[i].m_weight >= 0){ // insert
                result &= add_edge(graph::WeightedEdge{A[i].m_source, A[i].m_destination, A[i].m_weight});
            } else { // remove
                result &= remove_edge(graph::Edge{A[i].m_source, A[i].m_destination});
            }
        }
    }

    return result;
}

void UpdateInterface::load(const string& path) {
    auto reader = reader::Reader::open(path);
    ASSERT(reader->is_directed() == is_directed());
    graph::WeightedEdge edge;
    while(reader->read(edge)){
        add_vertex(edge.m_source);
        add_vertex(edge.m_destination);
        add_edge(edge);
    }
}

} // namespace library
