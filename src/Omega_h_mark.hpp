#ifndef OMEGA_H_MARK_HPP
#define OMEGA_H_MARK_HPP

#include <vector>

#include <Omega_h_array.hpp>
#include <Omega_h_mesh.hpp>

namespace Omega_h {

// should this be Graph&? Graph is potentially a large structure
Read<I8> mark_down(Graph low2high, Read<I8> marked_highs);
Read<I8> mark_down(
    Mesh const* mesh, Int high_dim, Int low_dim, Read<I8> marked_highs);
Read<I8> mark_up(Mesh const* mesh, Int low_dim, Int high_dim, Read<I8> low_marked);
Read<I8> mark_adj(Mesh const* mesh, Int from_dim, Int to_dim, Read<I8> from_marked);
Read<I8> mark_up_all(
    Mesh const* mesh, Int low_dim, Int high_dim, Read<I8> low_marked);
Read<I8> mark_by_class_dim(Mesh const* mesh, Int ent_dim, Int class_dim);
Read<I8> mark_by_class(Mesh const* mesh, Int ent_dim, Int class_dim, LO class_id);
Read<I8> mark_by_owner(Mesh const* mesh, Int ent_dim, I32 rank);
Read<I8> mark_dual_layers(Mesh const* mesh, Read<I8> marks, Int nlayers);
GO count_owned_marks(Mesh const* mesh, Int ent_dim, Read<I8> marks);
Read<I8> mark_sliver_layers(Mesh const* mesh, Real qual_ceil, Int nlayers);
Read<I8> mark_exposed_sides(Mesh const* mesh);
Read<I8> mark_class_closure(
    Mesh const* mesh, Int ent_dim, Int class_dim, LO class_id);

Read<I8> mark_class_closures(Mesh const* mesh, Int ent_dim, Int class_dim,
    std::vector<ClassId> const& class_ids);

Read<I8> mark_class_closures(Mesh const* mesh, Int class_dim,
    std::vector<ClassId> const& class_ids, Graph nodes2ents);

Read<I8> mark_class_closures(
    Mesh const* mesh, Int ent_dim, std::vector<ClassPair> const& class_pairs);

Read<I8> mark_class_closures(
    Mesh const* mesh, std::vector<ClassPair> const& class_pairs, Graph nodes2ents[4]);

template <typename T>
OMEGA_H_DEVICE Int binary_search(Read<T> const& a, T v, LO n) {
  LO l = 0;
  LO r = n - 1;
  while (1) {
    if (l > r) return -1;
    auto m = (l + r) / 2;
    auto a_m = a[m];
    if (a_m < v) {
      l = m + 1;
      continue;
    }
    if (a_m > v) {
      r = m - 1;
      continue;
    }
    return m;
  }
}

}  // end namespace Omega_h

#endif
