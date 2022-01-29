#include "Omega_h_mesh.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>

#include "Omega_h_array_ops.hpp"
#include "Omega_h_bcast.hpp"
#include "Omega_h_compare.hpp"
#include "Omega_h_element.hpp"
#include "Omega_h_for.hpp"
#include "Omega_h_ghost.hpp"
#include "Omega_h_inertia.hpp"
#include "Omega_h_map.hpp"
#include "Omega_h_mark.hpp"
#include "Omega_h_migrate.hpp"
#include "Omega_h_quality.hpp"
#include "Omega_h_shape.hpp"
#include "Omega_h_timer.hpp"
#include "Omega_h_int_scan.hpp"
#include "Omega_h_atomics.hpp"

namespace Omega_h {

Mesh::Mesh() {
  family_ = OMEGA_H_SIMPLEX;
  dim_ = -1;
  for (Int i = 0; i <= 3; ++i) nents_[i] = -1;
  for (Int i = 0; i <= 7; ++i) nents_type_[i] = -1;
  parting_ = -1;
  nghost_layers_ = -1;
  library_ = nullptr;
  matched_ = -1;
}

Mesh::Mesh(Library* library_in) : Mesh() { set_library(library_in); }

void Mesh::set_library(Library* library_in) {
  OMEGA_H_CHECK(library_in != nullptr);
  library_ = library_in;
}

Library* Mesh::library() const { return library_; }

void Mesh::set_comm(CommPtr const& new_comm) {
  auto rank_had_comm = bool(comm_);
  auto nnew_had_comm = new_comm->allreduce(I32(rank_had_comm), OMEGA_H_SUM);
  if (0 < nnew_had_comm && nnew_had_comm < new_comm->size()) {
    // partitioning out from small sub-communicator to larger one
    if (!rank_had_comm) {
      // temporarily set the uninit ranks to Comm::self()
      comm_ = library_->self();
    } else {
      /* forget RIB hints. this prevents some ranks from
         having hints while the new ranks do not, which would
         break RIB. also, since repartitioning does not change
         the geometric properties of the mesh and our RIB code
         is partition and order independent, it will recover
         the same axes of separation as before */
      rib_hints_ = RibPtr();
    }
    bcast_mesh(this, new_comm, rank_had_comm);
  }
  /* if some ranks already have mesh data, their
     parallel info needs updating, we'll do this
     by using the old Dist to set new owners */
  if (0 < nnew_had_comm && library_->world()->size() > 1) {
    for (Int d = 0; d <= dim(); ++d) {
      auto dist = ask_dist(d);
      dist.change_comm(new_comm);
      owners_[d].ranks = dist.items2ranks();
    }
  }
  comm_ = new_comm;
}

void Mesh::set_family(Omega_h_Family family_in) { family_ = family_in; }

void Mesh::set_matched(I8 is_matched) { matched_ = is_matched; }

void Mesh::set_dim(Int dim_in) {
  OMEGA_H_CHECK(dim_ == -1);
  OMEGA_H_CHECK(dim_in >= 1);
  OMEGA_H_CHECK(dim_in <= 3);
  dim_ = dim_in;
}

Int Mesh::ent_dim(Topo_type ent_type) const {
  Int ent_dim;
  if (int(ent_type) == 0) {
    ent_dim = 0;
  }
  else if (int(ent_type) == 1) {
    ent_dim = 1;
  }
  else if ((int(ent_type) > 1) && (int(ent_type) < 4)) {      
    ent_dim = 2;
  }
  else {
    ent_dim = 3;
  }
  return ent_dim;
}

void Mesh::set_verts(LO nverts_in) { nents_[VERT] = nverts_in; }

void Mesh::set_verts_type(LO nverts_in) { nents_type_[int(Topo_type::vertex)] = nverts_in; }

void Mesh::set_ents(Int ent_dim, Adj down) {
  OMEGA_H_TIME_FUNCTION;
  check_dim(ent_dim);
  OMEGA_H_CHECK(!has_ents(ent_dim));
  LOs hl2l = down.ab2b;
  auto deg = element_degree(family(), ent_dim, ent_dim - 1);
  nents_[ent_dim] = divide_no_remainder(hl2l.size(), deg);
  add_adj(ent_dim, ent_dim - 1, down);
}

void Mesh::set_model_ents(Int ent_dim, LOs Ids) {
  OMEGA_H_TIME_FUNCTION;
  check_dim(ent_dim);
  model_ents_[ent_dim] = Ids;
}

void Mesh::set_model_matches(Int ent_dim, LOs matches) {
  OMEGA_H_TIME_FUNCTION;
  check_dim(ent_dim+1);
  model_matches_[ent_dim] = matches;
}

LOs Mesh::get_model_ents(Int ent_dim) {
  OMEGA_H_TIME_FUNCTION;
  check_dim(ent_dim);
  return model_ents_[ent_dim];
}

LOs Mesh::get_model_matches(Int ent_dim) {
  OMEGA_H_TIME_FUNCTION;
  check_dim(ent_dim);
  return model_matches_[ent_dim];
}

void Mesh::set_ents(Topo_type high_type, Topo_type low_type, Adj h2l) {
  OMEGA_H_TIME_FUNCTION;
  check_type(high_type);
  check_type(low_type);
  if (int(high_type) < 6) {
    OMEGA_H_CHECK(!has_ents(high_type));
  }
  auto deg = element_degree(high_type, low_type);
  nents_type_[int(high_type)] = divide_no_remainder(h2l.ab2b.size(), deg);
  add_adj(high_type, low_type, h2l);
}

void Mesh::set_parents(Int ent_dim, Parents parents) {
  check_dim2(ent_dim);
  parents_[ent_dim] = std::make_shared<Parents>(parents);
}

CommPtr Mesh::comm() const { return comm_; }

LO Mesh::nents(Int ent_dim) const {
  check_dim2(ent_dim);
  return nents_[ent_dim];
}

LO Mesh::nents(Topo_type ent_type) const {
  check_type2(ent_type);
  return nents_type_[int(ent_type)];
}

LO Mesh::nelems() const { return nents(dim()); }

LO Mesh::nregions() const { return nents(REGION); }

LO Mesh::nfaces() const { return nents(FACE); }

LO Mesh::nedges() const { return nents(EDGE); }

LO Mesh::nverts() const { return nents(VERT); }

LO Mesh::npyrams() const { return nents(Topo_type::pyramid); }

LO Mesh::nwedges() const { return nents(Topo_type::wedge); }

LO Mesh::nhexs() const { return nents(Topo_type::hexahedron); }

LO Mesh::ntets() const { return nents(Topo_type::tetrahedron); }

LO Mesh::nquads() const { return nents(Topo_type::quadrilateral); }

LO Mesh::ntris() const { return nents(Topo_type::triangle); }

LO Mesh::nedges_mix() const { return nents(Topo_type::edge); }

LO Mesh::nverts_mix() const { return nents(Topo_type::vertex); }

LO Mesh::nregions_mix() const { 
  return (nents(Topo_type::tetrahedron) +
          nents(Topo_type::hexahedron) +
          nents(Topo_type::wedge) +
          nents(Topo_type::pyramid));
}

LO Mesh::nfaces_mix() const { 
  return (nents(Topo_type::triangle) +
          nents(Topo_type::quadrilateral));
}

GO Mesh::nglobal_ents(Int ent_dim) {
  if (!could_be_shared(ent_dim)) {
    return comm_->allreduce(GO(nents(ent_dim)), OMEGA_H_SUM);
  }
  auto nowned = get_sum(this->owned(ent_dim));
  return comm_->allreduce(GO(nowned), OMEGA_H_SUM);
}

template <typename T>
void Mesh::add_tag(Int ent_dim, std::string const& name, Int ncomps) {
  if (has_tag(ent_dim, name)) remove_tag(ent_dim, name);
  check_dim2(ent_dim);
  check_tag_name(name);
  OMEGA_H_CHECK(ncomps >= 0);
  OMEGA_H_CHECK(ncomps <= Int(INT8_MAX));
  OMEGA_H_CHECK(tags_[ent_dim].size() < size_t(INT8_MAX));
  TagPtr ptr(new Tag<T>(name, ncomps));
  tags_[ent_dim].push_back(std::move(ptr));
}

template <typename T>
void Mesh::add_tag(Topo_type ent_type, std::string const& name, Int ncomps) {
  if (has_tag(ent_type, name)) remove_tag(ent_type, name);
  check_type2(ent_type);
  check_tag_name(name);
  OMEGA_H_CHECK(ncomps >= 0);
  OMEGA_H_CHECK(ncomps <= Int(INT8_MAX));
  OMEGA_H_CHECK(tags_type_[int(ent_type)].size() < size_t(INT8_MAX));
  TagPtr ptr(new Tag<T>(name, ncomps));
  tags_type_[int(ent_type)].push_back(std::move(ptr));
}

template <typename T>
void Mesh::add_tag(Int ent_dim, std::string const& name, Int ncomps,
    Read<T> array, bool internal) {
  check_dim2(ent_dim);
  auto it = tag_iter(ent_dim, name);
  auto had_tag = (it != tags_[ent_dim].end());
  Tag<T>* tag;
  if (had_tag) {
    tag = as<T>(it->get());
    OMEGA_H_CHECK(ncomps == tag->ncomps());
  } else {
    check_tag_name(name);
    OMEGA_H_CHECK(ncomps >= 0);
    OMEGA_H_CHECK(ncomps <= Int(INT8_MAX));
    OMEGA_H_CHECK(tags_[ent_dim].size() < size_t(INT8_MAX));
    tag = new Tag<T>(name, ncomps);
    TagPtr ptr(tag);
    tags_[ent_dim].push_back(std::move(ptr));
  }
  /* internal typically indicates migration/adaptation/file reading,
     when we do not want any invalidation to take place.
     the invalidation is there to prevent users changing coordinates
     etc. without updating dependent fields */
  if (!internal) react_to_set_tag(ent_dim, name);
  tag->set_array(array);
}

template <typename T>
void Mesh::add_tag(Topo_type ent_type, std::string const& name, Int ncomps,
    Read<T> array, bool internal) {
  check_type2(ent_type);
  auto it = tag_iter(ent_type, name);
  auto had_tag = (it != tags_type_[int(ent_type)].end());
  Tag<T>* tag;
  if (had_tag) {
    tag = as<T>(it->get());
    OMEGA_H_CHECK(ncomps == tag->ncomps());
  } else {
    check_tag_name(name);
    OMEGA_H_CHECK(ncomps >= 0);
    OMEGA_H_CHECK(ncomps <= Int(INT8_MAX));
    OMEGA_H_CHECK(tags_type_[int(ent_type)].size() < size_t(INT8_MAX));
    tag = new Tag<T>(name, ncomps);
    TagPtr ptr(tag);
    tags_type_[int(ent_type)].push_back(std::move(ptr));
  }
  OMEGA_H_CHECK(array.size() == nents_type_[int(ent_type)] * ncomps);
  if (!internal) react_to_set_tag(ent_type, name);
  tag->set_array(array);
}

template <typename T>
void Mesh::set_tag(
    Int ent_dim, std::string const& name, Read<T> array, bool internal) {
  if (!has_tag(ent_dim, name)) {
    Omega_h_fail("set_tag(%s, %s): tag doesn't exist (use add_tag first)\n",
        topological_plural_name(family(), ent_dim), name.c_str());
  }
  Tag<T>* tag = as<T>(tag_iter(ent_dim, name)->get());
  /* internal typically indicates migration/adaptation/file reading,
     when we do not want any invalidation to take place.
     the invalidation is there to prevent users changing coordinates
     etc. without updating dependent fields */
  if (!internal) react_to_set_tag(ent_dim, name);
  tag->set_array(array);
}

template <typename T>
void Mesh::set_tag(
    Topo_type ent_type, std::string const& name, Read<T> array, bool internal) {
  if (!has_tag(ent_type, name)) {
    Omega_h_fail("set_tag(%s, %s): tag doesn't exist (use add_tag first)\n",
      dimensional_plural_name(ent_type), name.c_str());
  }
  Tag<T>* tag = as<T>(tag_iter(ent_type, name)->get());
  OMEGA_H_CHECK(array.size() == nents(ent_type) * tag->ncomps());
  if (!internal) react_to_set_tag(ent_type, name);
  tag->set_array(array);
}

void Mesh::react_to_set_tag(Int ent_dim, std::string const& name) {
  /* hardcoded cache invalidations */
  bool is_coordinates = (name == "coordinates");
  if ((ent_dim == VERT) && (is_coordinates || (name == "metric"))) {
    remove_tag(EDGE, "length");
    remove_tag(dim(), "quality");
  }
  if ((ent_dim == VERT) && is_coordinates) {
    remove_tag(dim(), "size");
  }
}

void Mesh::react_to_set_tag(Topo_type ent_type, std::string const& name) {
  bool is_coordinates = (name == "coordinates");
  if ((int(ent_type) == 0) && (is_coordinates || (name == "metric"))) {
    remove_tag(Topo_type::edge, "length");

    remove_tag(Topo_type::pyramid, "quality");
    remove_tag(Topo_type::wedge, "quality");
    remove_tag(Topo_type::hexahedron, "quality");
    remove_tag(Topo_type::tetrahedron, "quality");
    remove_tag(Topo_type::quadrilateral, "quality");
    remove_tag(Topo_type::triangle, "quality");
  }
  if ((int(ent_type) == 0) && is_coordinates) {
    remove_tag(Topo_type::pyramid, "size");
    remove_tag(Topo_type::wedge, "size");
    remove_tag(Topo_type::hexahedron, "size");
    remove_tag(Topo_type::tetrahedron, "size");
    remove_tag(Topo_type::quadrilateral, "size");
    remove_tag(Topo_type::triangle, "size");
  }
}

TagBase const* Mesh::get_tagbase(Int ent_dim, std::string const& name) const {
  check_dim2(ent_dim);
  auto it = tag_iter(ent_dim, name);
  if (it == tags_[ent_dim].end()) {
    Omega_h_fail("get_tagbase(%s, %s): doesn't exist\n",
        topological_plural_name(family(), ent_dim), name.c_str());
  }
  return it->get();
}

TagBase const* Mesh::get_tagbase(Topo_type ent_type, std::string const& name) const {
  check_type2(ent_type);
  auto it = tag_iter(ent_type, name);
  if (it == tags_type_[int(ent_type)].end()) {
    Omega_h_fail("get_tagbase(%s, %s): doesn't exist\n",
        dimensional_plural_name(ent_type), name.c_str());
  }
  return it->get();
}

template <typename T>
Tag<T> const* Mesh::get_tag(Int ent_dim, std::string const& name) const {
  return as<T>(get_tagbase(ent_dim, name));
}

template <typename T>
Tag<T> const* Mesh::get_tag(Topo_type ent_type, std::string const& name) const {
  return as<T>(get_tagbase(ent_type, name));
}

template <typename T>
Read<T> Mesh::get_array(Int ent_dim, std::string const& name) const {
  return get_tag<T>(ent_dim, name)->array();
}

template <typename T>
Read<T> Mesh::get_array(Topo_type ent_type, std::string const& name) const {
  return get_tag<T>(ent_type, name)->array();
}

void Mesh::remove_tag(Int ent_dim, std::string const& name) {
  if (!has_tag(ent_dim, name)) return;
  check_dim2(ent_dim);
  OMEGA_H_CHECK(has_tag(ent_dim, name));
  tags_[ent_dim].erase(tag_iter(ent_dim, name));
}

void Mesh::remove_tag(Topo_type ent_type, std::string const& name) {
  if (!has_tag(ent_type, name)) return;
  check_type2(ent_type);
  OMEGA_H_CHECK(has_tag(ent_type, name));
  tags_type_[int(ent_type)].erase(tag_iter(ent_type, name));
}

bool Mesh::has_tag(Int ent_dim, std::string const& name) const {
  check_dim(ent_dim);
  if (!has_ents(ent_dim)) return false;
  return tag_iter(ent_dim, name) != tags_[ent_dim].end();
}

bool Mesh::has_tag(Topo_type ent_type, std::string const& name) const {
  check_type(ent_type);
  if (!has_ents(ent_type)) return false;
  return tag_iter(ent_type, name) != tags_type_[int(ent_type)].end();
}

Int Mesh::ntags(Int ent_dim) const {
  check_dim2(ent_dim);
  return static_cast<Int>(tags_[ent_dim].size());
}

Int Mesh::ntags(Topo_type ent_type) const {
  check_type2(ent_type);
  return static_cast<Int>(tags_type_[int(ent_type)].size());
}

TagBase const* Mesh::get_tag(Int ent_dim, Int i) const {
  check_dim2(ent_dim);
  OMEGA_H_CHECK(0 <= i);
  OMEGA_H_CHECK(i <= ntags(ent_dim));
  return tags_[ent_dim][static_cast<std::size_t>(i)].get();
}

TagBase const* Mesh::get_tag(Topo_type ent_type, Int i) const {
  check_type2(ent_type);
  OMEGA_H_CHECK(0 <= i);
  OMEGA_H_CHECK(i <= ntags(ent_type));
  return tags_type_[int(ent_type)][static_cast<std::size_t>(i)].get();
}

bool Mesh::has_ents(Int ent_dim) const {
  check_dim(ent_dim);
  return nents_[ent_dim] >= 0;
}

bool Mesh::has_ents(Topo_type ent_type) const {
  check_type(ent_type);
  return nents_type_[int(ent_type)] >= 0;
}

bool Mesh::has_adj(Int from, Int to) const {
  check_dim(from);
  check_dim(to);
  return bool(adjs_[from][to]);
}

bool Mesh::has_adj(Topo_type from_type, Topo_type to_type) const {
  check_type(from_type);
  check_type(to_type);
  return bool(adjs_type_[int(from_type)][int(to_type)]);
}

Adj Mesh::get_adj(Int from, Int to) const {
  check_dim2(from);
  check_dim2(to);
  OMEGA_H_CHECK(has_adj(from, to));
  return *(adjs_[from][to]);
}

Adj Mesh::get_adj(Topo_type from_type, Topo_type to_type) const {
  check_type2(from_type);
  check_type2(from_type);
  OMEGA_H_CHECK(has_adj(from_type, to_type));
  return *(adjs_type_[int(from_type)][int(to_type)]);
}

Adj Mesh::ask_down(Int from, Int to) const {
  OMEGA_H_CHECK(to < from);
  return ask_adj(from, to);
}

Adj Mesh::ask_down(Topo_type from_type, Topo_type to_type) const {
  OMEGA_H_CHECK(int(to_type) < int(from_type));
  return ask_adj(from_type, to_type);
}

LOs Mesh::ask_verts_of(Int ent_dim) const { return ask_adj(ent_dim, VERT).ab2b; }

LOs Mesh::ask_verts_of(Topo_type ent_type) const { return ask_adj(ent_type, Topo_type::vertex).ab2b; }

LOs Mesh::ask_elem_verts() const { return ask_verts_of(dim()); }

Adj Mesh::ask_up(Int from, Int to) const {
  OMEGA_H_CHECK(from < to);
  return ask_adj(from, to);
}

Adj Mesh::ask_up(Topo_type from_type, Topo_type to_type) const {
  OMEGA_H_CHECK(int(from_type) < int(to_type));
  return ask_adj(from_type, to_type);
}

Graph Mesh::ask_star(Int ent_dim) const {
  OMEGA_H_CHECK(ent_dim < dim());
  return ask_adj(ent_dim, ent_dim);
}

Graph Mesh::ask_dual() const { return ask_adj(dim(), dim()); }

Mesh::TagIter Mesh::tag_iter(Int ent_dim, std::string const& name) {
  return std::find_if(tags_[ent_dim].begin(), tags_[ent_dim].end(),
      [&](TagPtr const& a) { return a->name() == name; });
}

Mesh::TagCIter Mesh::tag_iter(Int ent_dim, std::string const& name) const {
  return std::find_if(tags_[ent_dim].begin(), tags_[ent_dim].end(),
      [&](TagPtr const& a) { return a->name() == name; });
}

Mesh::TagIter Mesh::tag_iter(Topo_type ent_type, std::string const& name) {
  return std::find_if(tags_type_[int(ent_type)].begin(), tags_type_[int(ent_type)].end(),
      [&](TagPtr const& a) { return a->name() == name; });
}

Mesh::TagCIter Mesh::tag_iter(Topo_type ent_type, std::string const& name) const {
  return std::find_if(tags_type_[int(ent_type)].begin(), tags_type_[int(ent_type)].end(),
      [&](TagPtr const& a) { return a->name() == name; });
}

void Mesh::check_dim(Int ent_dim) const {
  OMEGA_H_CHECK(0 <= ent_dim);
  OMEGA_H_CHECK(ent_dim <= dim());
}

void Mesh::check_dim2(Int ent_dim) const {
  check_dim(ent_dim);
  OMEGA_H_CHECK(has_ents(ent_dim));
}

void Mesh::check_type(Topo_type ent_type) const {
  OMEGA_H_CHECK(Topo_type::vertex <= ent_type);
  OMEGA_H_CHECK(ent_type <= Topo_type::pyramid);
}

void Mesh::check_type2(Topo_type ent_type) const {
  check_type(ent_type);
  OMEGA_H_CHECK(has_ents(ent_type));
}

void Mesh::add_adj(Int from, Int to, Adj adj) {
  check_dim2(from);
  check_dim(to);
  OMEGA_H_CHECK(adj.ab2b.exists());
  if (to < from) {
    OMEGA_H_CHECK(!adj.a2ab.exists());
    if (to == VERT) {
      OMEGA_H_CHECK(!adj.codes.exists());
    } else {
      OMEGA_H_CHECK(adj.codes.exists());
    }
    OMEGA_H_CHECK(
        adj.ab2b.size() == nents(from) * element_degree(family(), from, to));
  } else {
    if (from < to) {
      OMEGA_H_CHECK(adj.a2ab.exists());
      OMEGA_H_CHECK(adj.codes.exists());
      OMEGA_H_CHECK(
          adj.ab2b.size() == nents(to) * element_degree(family(), to, from));
    }
    OMEGA_H_CHECK(adj.a2ab.size() == nents(from) + 1);
  }
  adjs_[from][to] = std::make_shared<Adj>(adj);
}

void Mesh::add_adj(Topo_type from_type, Topo_type to_type, Adj adj) {
  check_type2(from_type);
  check_type(to_type);
  OMEGA_H_CHECK(adj.ab2b.exists());
  const int from = int(from_type);
  const int to = int(to_type); 

  if (to < from) {
    OMEGA_H_CHECK(!adj.a2ab.exists());                         
    if (to_type == Topo_type::vertex) {
      OMEGA_H_CHECK(!adj.codes.exists());
    } else {
      OMEGA_H_CHECK(adj.codes.exists());
    }
    OMEGA_H_CHECK(                                            
        adj.ab2b.size() == nents(from_type) * element_degree(from_type, to_type));
  } else {
    if (from < to) {
      OMEGA_H_CHECK(adj.a2ab.exists());                        
      OMEGA_H_CHECK(adj.codes.exists());
      OMEGA_H_CHECK(
          adj.ab2b.size() == nents(to_type) * element_degree(to_type, from_type)); 
    }
    OMEGA_H_CHECK(adj.a2ab.size() == nents(from_type) + 1);         
  }
  adjs_type_[from][to] = std::make_shared<Adj>(adj);
}

Adj Mesh::derive_adj(Int from, Int to) const {
  OMEGA_H_TIME_FUNCTION;
  check_dim(from);
  check_dim2(to);
  if (from < to) {
    Adj down = ask_adj(to, from);
    Int nlows_per_high = element_degree(family(), to, from);
    LO nlows = nents(from);
    Adj up = invert_adj(down, nlows_per_high, nlows, to, from);
    return up;
  } else if (to < from) {
    OMEGA_H_CHECK(to + 1 < from);
    Adj h2m = ask_adj(from, to + 1);
    Adj m2l = ask_adj(to + 1, to);
    Adj h2l = transit(h2m, m2l, family_, from, to);
    return h2l;
  } else {
    if (from == dim() && to == dim()) {
      return elements_across_sides(dim(), ask_adj(dim(), dim() - 1),
          ask_adj(dim() - 1, dim()), mark_exposed_sides(this));
    }
    if (from == VERT && to == VERT) {
      return verts_across_edges(ask_adj(EDGE, VERT), ask_adj(VERT, EDGE));
    }
    if (from == EDGE && to == EDGE) {
      OMEGA_H_CHECK(dim() >= 2);
      Graph g = edges_across_tris(ask_adj(FACE, EDGE), ask_adj(EDGE, FACE));
      if (dim() == 3) {
        g = add_edges(
            g, edges_across_tets(ask_adj(REGION, EDGE), ask_adj(EDGE, REGION)));
      }
      return g;
    }
  }
  Omega_h_fail("can't derive adjacency from %s to %s\n",
      topological_plural_name(family(), from),
      topological_plural_name(family(), to));
  OMEGA_H_NORETURN(Adj());
}

Adj Mesh::derive_adj(Topo_type from_type, Topo_type to_type) const {
  OMEGA_H_TIME_FUNCTION;
  check_type(from_type);
  check_type2(to_type);
  const int from = int(from_type);
  const int to = int(to_type);
  if (from < to) {
    Adj down = ask_adj(to_type, from_type);
    Int nlows_per_high = element_degree(to_type, from_type);
    LO nlows = nents(from_type);
    Adj up = invert_adj(down, nlows_per_high, nlows, to_type, from_type);
    return up;
  }
  else if (to < from) {
    OMEGA_H_CHECK(to + 1 < from);
    Topo_type mid_type;
    if (to_type == Topo_type::vertex) {
      mid_type = Topo_type::edge;
    }
    else if ((from_type == Topo_type::tetrahedron) ||
             (from_type == Topo_type::pyramid)) {
      mid_type = Topo_type::triangle;
    }
    else {
      mid_type = Topo_type::quadrilateral;
    }
    Adj h2m = ask_adj(from_type, mid_type);
    Adj m2l = ask_adj(mid_type, to_type);
    Adj h2l = transit(h2m, m2l, from_type, to_type, mid_type); 
    return h2l;
  }
  /* todo: add second order adjacency derivation */
  Omega_h_fail("can't derive adjacency from %s to %s\n",
      dimensional_plural_name(from_type),
      dimensional_plural_name(to_type));
  OMEGA_H_NORETURN(Adj());
}

Adj Mesh::ask_adj(Int from, Int to) const {
  OMEGA_H_TIME_FUNCTION;
  check_dim2(from);
  check_dim2(to);
  if (has_adj(from, to)) {
    return get_adj(from, to);
  }
  Adj derived = derive_adj(from, to);
  adjs_[from][to] = std::make_shared<Adj>(derived);
  return derived;
}

Adj Mesh::ask_adj(Topo_type from_type, Topo_type to_type) const {
  OMEGA_H_TIME_FUNCTION;
  check_type2(from_type);
  check_type2(to_type);
  if (has_adj(from_type, to_type)) {
    return get_adj(from_type, to_type);
  }
  Adj derived = derive_adj(from_type, to_type);
  adjs_type_[int(from_type)][int(to_type)] = std::make_shared<Adj>(derived);
  return derived;
}

void Mesh::add_coords(Reals array) {
  add_tag<Real>(0, "coordinates", dim(), array);
}

Reals Mesh::coords() const { return get_array<Real>(0, "coordinates"); }

void Mesh::set_coords(Reals const& array) {
  OMEGA_H_CHECK(array.size() == nverts() * dim());
  set_tag<Real>(VERT, "coordinates", array);
}

void Mesh::add_coords_mix(Reals array) {
  add_tag<Real>(Topo_type::vertex, "coordinates", dim(), array);
}

Reals Mesh::coords_mix() const { return get_array<Real>(Topo_type::vertex, "coordinates"); }

Read<GO> Mesh::globals(Int ent_dim) const {
  return get_array<GO>(ent_dim, "global");
}

// FIXME removed memoization to make function const. It's nonsensical that a function "ask_lengths" is adding a mesh tag with externally visible side effects!
Reals Mesh::ask_lengths() const {
  //if (!has_tag(EDGE, "length")) {
  //  auto lengths = measure_edges_metric(this);
  //  add_tag(EDGE, "length", 1, lengths);
  //}
  //return get_array<Real>(EDGE, "length");
  return measure_edges_metric(this);
}

// FIXME memoization removed to avoid tag storage which is nonsensical
Reals Mesh::ask_qualities() const {
  //if (!has_tag(dim(), "quality")) {
  //  auto qualities = measure_qualities(this);
  //}
  //return get_array<Real>(dim(), "quality");
  return measure_qualities(this);
}

// FIXME memoization removed to make function const since tags are used...
Reals Mesh::ask_sizes() const {
  /*
  if (!has_tag(dim(), "size")) {
    auto sizes = measure_elements_real(this);
    add_tag(dim(), "size", 1, sizes);
  }
  return get_array<Real>(dim(), "size");
  */
  return measure_elements_real(this);
}

// FIXME memoization removed to alleviate side effects by removing use of tags
Bytes Mesh::ask_levels(Int ent_dim) const {
  check_dim2(ent_dim);
  /*
  if (!has_tag(ent_dim, "level")) {
    auto levels = Bytes(nents(ent_dim), 0);
    add_tag(ent_dim, "level", 1, levels);
  }
  return get_array<Byte>(ent_dim, "level");
  */
  return Bytes(nents(ent_dim), 0);
}

// FIXME removed memoization to get rid of tag usage in what should be const func
Bytes Mesh::ask_leaves(Int ent_dim) const {
  check_dim2(ent_dim);
  /*
  if (!has_tag(ent_dim, "leaf")) {
    auto leaves = Bytes(nents(ent_dim), 1);
    add_tag(ent_dim, "leaf", 1, leaves);
  }
  return get_array<Byte>(ent_dim, "leaf");
  */
  return Bytes(nents(ent_dim), 1);
}

Parents Mesh::ask_parents(Int child_dim) const {
  check_dim2(child_dim);
  if (!parents_[child_dim]) {
    auto parent_idx = LOs(nents(child_dim), -1);
    auto codes = Read<I8>(nents(child_dim), 0);
    Parents p(parent_idx, codes);
    parents_[child_dim] = std::make_shared<Parents>(p);
  }
  return *(parents_[child_dim]);
}

Children Mesh::ask_children(Int parent_dim, Int child_dim) const {
  check_dim2(parent_dim);
  auto nparent_dim_ents = nents(parent_dim);
  auto c2p = ask_parents(child_dim);
  if (!children_[parent_dim][child_dim]) {
    auto c = invert_parents(c2p, parent_dim, nparent_dim_ents);
    children_[parent_dim][child_dim] = std::make_shared<Children>(c);
  }
  return *(children_[parent_dim][child_dim]);
}

bool Mesh::has_any_parents() const {
  bool has_parents = false;
  for (Int d = 0; d <= dim_; ++d) {
    if (parents_[d]) {
      has_parents = true;
    }
  }
  return has_parents;
}

void Mesh::set_owners(Int ent_dim, Remotes owners) {
  check_dim2(ent_dim);
  OMEGA_H_CHECK(nents(ent_dim) == owners.ranks.size());
  OMEGA_H_CHECK(nents(ent_dim) == owners.idxs.size());
  owners_[ent_dim] = owners;
  dists_[ent_dim] = DistPtr();
}

void Mesh::set_match_owners(Int ent_dim, Remotes match_owners) {
  check_dim2(ent_dim);
  OMEGA_H_CHECK(nents(ent_dim) == match_owners.ranks.size());
  OMEGA_H_CHECK(nents(ent_dim) == match_owners.idxs.size());
  match_owners_[ent_dim] = match_owners;
}

Remotes Mesh::ask_owners(Int ent_dim) const {
  if (!owners_[ent_dim].ranks.exists() || !owners_[ent_dim].idxs.exists()) {
    OMEGA_H_CHECK(comm_->size() == 1);
    owners_[ent_dim] = Remotes(
        Read<I32>(nents(ent_dim), comm_->rank()), LOs(nents(ent_dim), 0, 1));
  }
  return owners_[ent_dim];
}

Remotes Mesh::ask_match_owners(Int ent_dim) {
  if (!match_owners_[ent_dim].ranks.exists() || !match_owners_[ent_dim].idxs.exists()) {
    OMEGA_H_CHECK(comm_->size() == 1);
    Omega_h_fail(" Match owners for dim %d don't exist\n", ent_dim);
  }
  return match_owners_[ent_dim];
}

void Mesh::set_matches(Int ent_dim, c_Remotes matches) {
  check_dim2(ent_dim);
  check_dim2(ent_dim + 1);//matches will be always 1d less than ents
  matches_[ent_dim] = matches;
}

c_Remotes Mesh::get_matches(Int ent_dim) {
  check_dim2(ent_dim);
  check_dim2(ent_dim + 1);
  return matches_[ent_dim];
}

Read<I8> Mesh::owned(Int ent_dim) const {
  auto e2rank = ask_owners(ent_dim).ranks;
  return each_eq_to(e2rank, comm()->rank());
}

Dist Mesh::ask_dist(Int ent_dim) const {
  if (!dists_[ent_dim]) {
    auto owners = ask_owners(ent_dim);
    OMEGA_H_CHECK(owners.ranks.exists());
    OMEGA_H_CHECK(owners.idxs.exists());
    dists_[ent_dim] = std::make_shared<Dist>(comm_, owners, nents(ent_dim));
  }
  return *(dists_[ent_dim]);
}

Omega_h_Parting Mesh::parting() const {
  OMEGA_H_CHECK(parting_ != -1);
  return Omega_h_Parting(parting_);
}

Int Mesh::nghost_layers() const { return nghost_layers_; }

void Mesh::set_parting(Omega_h_Parting parting_in, Int nlayers, bool verbose) {
  if (verbose && comm_->rank() == 0) {
    std::cout << "going to ";
    switch (parting_in) {
      case OMEGA_H_ELEM_BASED:
        std::cout << "element based";
        break;
      case OMEGA_H_VERT_BASED:
        std::cout << "vertex based";
        break;
      case OMEGA_H_GHOSTED:
        std::cout << "ghosted (" << nlayers << " layers)";
        break;
    }
    std::cout << " partitioning\n";
  }
  if (parting_ == -1) {
    parting_ = parting_in;
    nghost_layers_ = nlayers;
    return;
  }
  if (parting_ == parting_in && nghost_layers_ == nlayers) {
    return;
  }
  if (parting_in == OMEGA_H_ELEM_BASED) {
    OMEGA_H_CHECK(nlayers == 0);
    if (comm_->size() > 1) partition_by_elems(this, verbose);
  } else if (parting_in == OMEGA_H_GHOSTED) {
    if (parting_ != OMEGA_H_GHOSTED || nlayers < nghost_layers_) {
      set_parting(OMEGA_H_ELEM_BASED, 0, false);
    }
    if (comm_->size() > 1) ghost_mesh(this, nlayers, verbose);
  } else if (parting_in == OMEGA_H_VERT_BASED) {
    OMEGA_H_CHECK(nlayers == 1);
    if (comm_->size() > 1) partition_by_verts(this, verbose);
  }
  parting_ = parting_in;
  nghost_layers_ = nlayers;
}

void Mesh::set_parting(Omega_h_Parting parting_in, bool verbose) {
  if (parting_in == OMEGA_H_ELEM_BASED)
    set_parting(parting_in, 0, verbose);
  else
    set_parting(parting_in, 1, verbose);
}

/* this is a member function mainly because it
   modifies the RIB hints */
void Mesh::balance(bool predictive) {
  OMEGA_H_TIME_FUNCTION;
  if (comm_->size() == 1) return;
  set_parting(OMEGA_H_ELEM_BASED);
  inertia::Rib hints;
  if (rib_hints_) hints = *rib_hints_;
  auto ecoords =
      average_field(this, dim(), LOs(nelems(), 0, 1), dim(), coords());
  if (dim() < 3) ecoords = resize_vectors(ecoords, dim(), 3);
  Reals masses;
  Real abs_tol;
  if (predictive) {
    masses = get_complexity_per_elem(this, get_array<Real>(VERT, "metric"));
    /* average between input mesh weight (1.0)
       and predicted output mesh weight */
    masses = add_to_each(masses, 1.);
    masses = multiply_each_by(masses, 1. / 2.);
    abs_tol = max2(0.0, get_max(comm_, masses));
  } else {
    masses = Reals(nelems(), 1);
    abs_tol = 1.0;
  }
  abs_tol *= 2.0;  // fudge factor ?
  auto owners = ask_owners(dim());
  recursively_bisect(comm(), abs_tol, &ecoords, &masses, &owners, &hints);
  rib_hints_ = std::make_shared<inertia::Rib>(hints);
  auto unsorted_new2owners = Dist(comm_, owners, nelems());
  auto owners2new = unsorted_new2owners.invert();
  auto owner_globals = this->globals(dim());
  owners2new.set_dest_globals(owner_globals);
  auto sorted_new2owners = owners2new.invert();
  migrate_mesh(this, sorted_new2owners, OMEGA_H_ELEM_BASED, false);
}

void Mesh::swap_root_owner(Int dim) {
  OMEGA_H_CHECK(this->is_matched());
  auto matches = this->get_matches(dim);
  auto leaf_idxs_r = matches.leaf_idxs;
  auto root_idxs_r = matches.root_idxs;
  auto root_ranks_r = matches.root_ranks;
  Write<LO> root_idxs_w(matches.leaf_idxs.size(), -1);
  Write<LO> root_ranks_w(matches.leaf_idxs.size(), -1);
  auto owners = this->ask_owners(dim);
  auto owner_idxs_r = owners.idxs;
  auto owner_ranks_r = owners.ranks;
  Write<LO> owner_idxs_w(owners.ranks.size(), -1);
  Write<LO> owner_ranks_w(owners.ranks.size(), -1);
  auto copy_owners = OMEGA_H_LAMBDA (LO i) {
    owner_idxs_w[i] = owner_idxs_r[i];
    owner_ranks_w[i] = owner_ranks_r[i];
  };
  parallel_for(owner_idxs_r.size(), copy_owners);
  auto swap = OMEGA_H_LAMBDA (LO i) {
    auto leaf = leaf_idxs_r[i];
    owner_idxs_w[leaf] = root_idxs_r[i];
    owner_ranks_w[leaf] = root_ranks_r[i];
    root_idxs_w[i] = owner_idxs_r[leaf];
    root_ranks_w[i] = owner_ranks_r[leaf];
  };
  parallel_for(leaf_idxs_r.size(), swap);
  this->set_owners(dim, Remotes(LOs(owner_ranks_w), LOs(owner_idxs_w)));
  this->set_matches(dim, 
    c_Remotes(leaf_idxs_r, LOs(root_idxs_w), LOs(root_ranks_w)));
  return;
}

void Mesh::balance(Reals weights) {
  OMEGA_H_TIME_FUNCTION;
  if (comm_->size() == 1) return;
  set_parting(OMEGA_H_ELEM_BASED);
  inertia::Rib hints;
  if (rib_hints_) hints = *rib_hints_;
  auto ecoords =
      average_field(this, dim(), LOs(nelems(), 0, 1), dim(), coords());
  if (dim() < 3) ecoords = resize_vectors(ecoords, dim(), 3);
  Real abs_tol;
  abs_tol = max2(0.0, get_max(comm_, weights));
  abs_tol *= 2.0;
  auto owners = ask_owners(dim());
  recursively_bisect(comm(), abs_tol, &ecoords, &weights, &owners, &hints);
  rib_hints_ = std::make_shared<inertia::Rib>(hints);
  auto unsorted_new2owners = Dist(comm_, owners, nelems());
  auto owners2new = unsorted_new2owners.invert();
  auto owner_globals = this->globals(dim());
  owners2new.set_dest_globals(owner_globals);
  auto sorted_new2owners = owners2new.invert();
  migrate_mesh(this, sorted_new2owners, OMEGA_H_ELEM_BASED, false);
  return;
}

Graph Mesh::ask_graph(Int from, Int to) {
  if (to > from) {
    return ask_up(from, to);
  }
  if (to < from) {
    auto down = ask_down(from, to);
    auto a2ab = LOs(nents(from) + 1, 0, element_degree(family(), from, to));
    return Graph(a2ab, down.ab2b);
  }
  OMEGA_H_CHECK(from == to);
  return identity_graph(nents(from));
}

template <typename T>
Read<T> Mesh::sync_array(Int ent_dim, Read<T> a, Int width) const {
  if (!could_be_shared(ent_dim)) return a;
  return ask_dist(ent_dim).invert().exch(a, width);
}

template <typename T>
Read<T> Mesh::sync_array_matched(Int ent_dim, Read<T> a, Int width) {
  if (!could_be_shared(ent_dim)) return a;
  swap_root_owner(ent_dim);
  return ask_dist(ent_dim).invert().exch(a, width);
}

template <typename T>
Future<T> Mesh::isync_array(Int ent_dim, Read<T> a, Int width) {
  if (!could_be_shared(ent_dim)) {
    return Future<T>(a);
  }
  return ask_dist(ent_dim).invert().iexch(a, width);
}

template <typename T>
Read<T> Mesh::sync_subset_array(
    Int ent_dim, Read<T> a_data, LOs a2e, T default_val, Int width) {
  if (!could_be_shared(ent_dim)) return a_data;
  auto e_data = map_onto(a_data, a2e, nents(ent_dim), default_val, width);
  e_data = sync_array(ent_dim, e_data, width);
  return unmap(a2e, e_data, width);
}

template <typename T>
Read<T> Mesh::reduce_array(Int ent_dim, Read<T> a, Int width, Omega_h_Op op) const {
  if (!could_be_shared(ent_dim)) return a;
  return ask_dist(ent_dim).exch_reduce(a, width, op);
}

template <typename T>
Read<T> Mesh::owned_array(Int ent_dim, Read<T> a, Int width) {
  OMEGA_H_CHECK(a.size() == width * nents(ent_dim));
  if (!could_be_shared(ent_dim)) return a;
  auto o = owned(ent_dim);
  auto o2e = collect_marked(o);
  return unmap(o2e, a, width);
}

void Mesh::sync_tag(Int ent_dim, std::string const& name) {
  auto tagbase = get_tagbase(ent_dim, name);
  switch (tagbase->type()) {
    case OMEGA_H_I8: {
      auto out =
          sync_array(ent_dim, as<I8>(tagbase)->array(), tagbase->ncomps());
      set_tag(ent_dim, name, out);
      break;
    }
    case OMEGA_H_I32: {
      auto out =
          sync_array(ent_dim, as<I32>(tagbase)->array(), tagbase->ncomps());
      set_tag(ent_dim, name, out);
      break;
    }
    case OMEGA_H_I64: {
      auto out =
          sync_array(ent_dim, as<I64>(tagbase)->array(), tagbase->ncomps());
      set_tag(ent_dim, name, out);
      break;
    }
    case OMEGA_H_F64: {
      auto out =
          sync_array(ent_dim, as<Real>(tagbase)->array(), tagbase->ncomps());
      set_tag(ent_dim, name, out);
      break;
    }
  }
}
void Mesh::sync_tag_matched(Int ent_dim, std::string const& name) {
  auto tagbase = get_tagbase(ent_dim, name);
  switch (tagbase->type()) {
    case OMEGA_H_I8: {
      auto out =
          sync_array_matched(ent_dim, as<I8>(tagbase)->array(), tagbase->ncomps());
      set_tag(ent_dim, name, out);
      swap_root_owner(ent_dim);
      break;
    }
    case OMEGA_H_I32: {
      auto out =
          sync_array_matched(ent_dim, as<I32>(tagbase)->array(), tagbase->ncomps());
      set_tag(ent_dim, name, out);
      swap_root_owner(ent_dim);
      break;
    }
    case OMEGA_H_I64: {
      auto out =
          sync_array_matched(ent_dim, as<I64>(tagbase)->array(), tagbase->ncomps());
      set_tag(ent_dim, name, out);
      swap_root_owner(ent_dim);
      break;
    }
    case OMEGA_H_F64: {
      auto out =
          sync_array_matched(ent_dim, as<Real>(tagbase)->array(), tagbase->ncomps());
      set_tag(ent_dim, name, out);
      swap_root_owner(ent_dim);
      break;
    }
  }
}

void Mesh::reduce_tag(Int ent_dim, std::string const& name, Omega_h_Op op) {
  auto tagbase = get_tagbase(ent_dim, name);
  switch (tagbase->type()) {
    case OMEGA_H_I8: {
      auto out = reduce_array(
          ent_dim, as<I8>(tagbase)->array(), tagbase->ncomps(), op);
      set_tag(ent_dim, name, out);
      break;
    }
    case OMEGA_H_I32: {
      auto out = reduce_array(
          ent_dim, as<I32>(tagbase)->array(), tagbase->ncomps(), op);
      set_tag(ent_dim, name, out);
      break;
    }
    case OMEGA_H_I64: {
      auto out = reduce_array(
          ent_dim, as<I64>(tagbase)->array(), tagbase->ncomps(), op);
      set_tag(ent_dim, name, out);
      break;
    }
    case OMEGA_H_F64: {
      auto out = reduce_array(
          ent_dim, as<Real>(tagbase)->array(), tagbase->ncomps(), op);
      set_tag(ent_dim, name, out);
      break;
    }
  }
}

bool Mesh::operator==(Mesh& other) {
  auto opts = MeshCompareOpts::init(this, VarCompareOpts::zero_tolerance());
  return OMEGA_H_SAME == compare_meshes(this, &other, opts, false);
}

Real Mesh::min_quality() {
  OMEGA_H_TIME_FUNCTION;
  return get_min(comm_, ask_qualities());
}

Real Mesh::max_length() {
  OMEGA_H_TIME_FUNCTION;
  return get_max(comm_, ask_lengths());
}

bool Mesh::could_be_shared(Int ent_dim) const {
  return !((comm_->size() == 1) ||
           (parting_ == OMEGA_H_ELEM_BASED && ent_dim == dim()));
}

bool Mesh::owners_have_all_upward(Int ent_dim) const {
  return have_all_upward() ||
         (parting_ == OMEGA_H_VERT_BASED && ent_dim == VERT);
}

bool Mesh::have_all_upward() const {
  return (comm_->size() == 1) || (parting_ == OMEGA_H_GHOSTED);
}

Mesh Mesh::copy_meta() const {
  Mesh m(library_);
  m.family_ = this->family_;
  m.dim_ = this->dim_;
  m.comm_ = this->comm_;
  m.parting_ = this->parting_;
  m.nghost_layers_ = this->nghost_layers_;
  m.rib_hints_ = this->rib_hints_;
  m.class_sets = this->class_sets;
  if (this->matched_ > 0) {
    m.matched_ = this->matched_;
    for (LO d = 0; d<DIMS; ++d) {
      m.model_ents_[d] = this->model_ents_[d];
      m.model_matches_[d] = this->model_matches_[d];
    }
  }
  return m;
}

Mesh::RibPtr Mesh::rib_hints() const { return rib_hints_; }

void Mesh::set_rib_hints(RibPtr hints) { rib_hints_ = hints; }

Real Mesh::imbalance(Int ent_dim) const {
  if (ent_dim == -1) ent_dim = dim();
  auto local = Real(nents(ent_dim));
  auto s = comm_->allreduce(local, OMEGA_H_SUM);
  if (s == 0.0) return 1.0;
  auto m = comm_->allreduce(local, OMEGA_H_MAX);
  auto n = comm_->size();
  auto a = s / n;
  return m / a;
}

bool can_print(Mesh* mesh) {
  return (!mesh->library()->silent_) && (mesh->comm()->rank() == 0);
}

Real repro_sum_owned(Mesh* mesh, Int ent_dim, Reals a) {
  return repro_sum(mesh->comm(), mesh->owned_array(ent_dim, a, 1));
}

Reals average_field(Mesh* mesh, Int ent_dim, LOs a2e, Int ncomps, Reals v2x) {
  OMEGA_H_TIME_FUNCTION;
  OMEGA_H_CHECK(v2x.size() % ncomps == 0);
  if (ent_dim == 0) return unmap(a2e, v2x, ncomps);
  auto ev2v = mesh->ask_verts_of(ent_dim);
  auto degree = element_degree(mesh->family(), ent_dim, VERT);
  auto na = a2e.size();
  Write<Real> out(na * ncomps);
  auto f = OMEGA_H_LAMBDA(LO a) {
    auto e = a2e[a];
    for (Int j = 0; j < ncomps; ++j) {
      Real comp = 0;
      for (Int k = 0; k < degree; ++k) {
        auto v = ev2v[e * degree + k];
        comp += v2x[v * ncomps + j];
      }
      comp /= degree;
      out[a * ncomps + j] = comp;
    }
  };
  parallel_for(na, f, "average_field");
  return out;
}

Reals average_field(Mesh* mesh, Int ent_dim, Int ncomps, Reals v2x) {
  auto a2e = LOs(mesh->nents(ent_dim), 0, 1);
  return average_field(mesh, ent_dim, a2e, ncomps, v2x);
}

void get_all_dim_tags(Mesh* mesh, Int dim, TagSet* tags) {
  for (Int j = 0; j < mesh->ntags(dim); ++j) {
    auto tagbase = mesh->get_tag(dim, j);
    (*tags)[size_t(dim)].insert(tagbase->name());
  }
}

void get_all_type_tags(Mesh* mesh, Int dim, Topo_type ent_type, TagSet* tags) {
  for (Int j = 0; j < mesh->ntags(ent_type); ++j) {
    auto tagbase = mesh->get_tag(ent_type, j);
    (*tags)[size_t(dim)].insert(tagbase->name());
  }
}

TagSet get_all_mesh_tags(Mesh* mesh) {
  TagSet out;
  for (Int i = 0; i <= mesh->dim(); ++i) {
    for (Int j = 0; j < mesh->ntags(i); ++j) {
      auto tagbase = mesh->get_tag(i, j);
      out[size_t(i)].insert(tagbase->name());
    }
  }
  return out;
}

void ask_for_mesh_tags(Mesh* mesh, TagSet const& tags) {
  if (tags[EDGE].count("length")) mesh->ask_lengths();
  if (tags[size_t(mesh->dim())].count("quality")) mesh->ask_qualities();
}

static std::vector<ClassPair> to_class_pairs(
    Mesh* mesh, std::set<std::string> const& class_names) {
  std::set<ClassPair> class_pairs;
  for (auto& cn : class_names) {
    auto it = mesh->class_sets.find(cn);
    if (it == mesh->class_sets.end()) {
      Omega_h_fail(
          "classification set name \"%s\" "
          "has no associated (dim,ID) pairs!\n",
          cn.c_str());
    }
    for (auto& p : it->second) {
      class_pairs.insert(p);
    }
  }
  std::vector<ClassPair> class_pair_vector(
      class_pairs.begin(), class_pairs.end());
  return class_pair_vector;
}

LOs ents_on_closure(
    Mesh* mesh, std::set<std::string> const& class_names, Int ent_dim) {
  auto class_pairs = to_class_pairs(mesh, class_names);
  auto ents_are_on = mark_class_closures(mesh, ent_dim, class_pairs);
  return collect_marked(ents_are_on);
}

LOs nodes_on_closure(
    Mesh* mesh, std::set<std::string> const& class_names, Graph nodes2ents[4]) {
  auto class_pairs = to_class_pairs(mesh, class_names);
  auto nodes_are_on = mark_class_closures(mesh, class_pairs, nodes2ents);
  return collect_marked(nodes_are_on);
}

#ifdef OMEGA_H_USE_CUDA
__host__
#endif
    void
    assign(Mesh& a, Mesh const& b) {
  a = b;
}

#define OMEGA_H_INST(T)                                                        \
  template Tag<T> const* Mesh::get_tag<T>(Int dim, std::string const& name)    \
      const;                                                                   \
  template Tag<T> const* Mesh::get_tag<T>(Topo_type ent_type, std::string const& name)    \
      const;                                                                   \
  template Read<T> Mesh::get_array<T>(Int dim, std::string const& name) const; \
  template Read<T> Mesh::get_array<T>(Topo_type ent_type, std::string const& name) const; \
  template void Mesh::add_tag<T>(                                              \
      Int dim, std::string const& name, Int ncomps);                           \
  template void Mesh::add_tag<T>(                                              \
      Topo_type ent_type, std::string const& name, Int ncomps);                           \
  template void Mesh::add_tag<T>(Int dim, std::string const& name, Int ncomps, \
      Read<T> array, bool internal);                                           \
  template void Mesh::add_tag<T>(Topo_type ent_type, std::string const& name, Int ncomps, \
      Read<T> array, bool internal);                                           \
  template void Mesh::set_tag(                                                 \
      Int dim, std::string const& name, Read<T> array, bool internal);         \
  template void Mesh::set_tag(                                                 \
      Topo_type ent_type, std::string const& name, Read<T> array, bool internal);         \
  template Read<T> Mesh::sync_array(Int ent_dim, Read<T> a, Int width) const;  \
  template Future<T> Mesh::isync_array(Int ent_dim, Read<T> a, Int width);     \
  template Read<T> Mesh::owned_array(Int ent_dim, Read<T> a, Int width);       \
  template Read<T> Mesh::sync_subset_array(                                    \
      Int ent_dim, Read<T> a_data, LOs a2e, T default_val, Int width);         \
  template Read<T> Mesh::reduce_array(                                         \
      Int ent_dim, Read<T> a, Int width, Omega_h_Op op) const;
OMEGA_H_INST(I8)
OMEGA_H_INST(I32)
OMEGA_H_INST(I64)
OMEGA_H_INST(Real)
#undef OMEGA_H_INST

}  // end namespace Omega_h
