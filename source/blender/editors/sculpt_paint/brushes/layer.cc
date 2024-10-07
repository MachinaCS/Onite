/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "editors/sculpt_paint/brushes/types.hh"

#include "DNA_brush_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_key.hh"
#include "BKE_mesh.hh"
#include "BKE_paint.hh"
#include "BKE_pbvh.hh"
#include "BKE_subdiv_ccg.hh"

#include "BLI_array.hh"
#include "BLI_enumerable_thread_specific.hh"
#include "BLI_math_vector.h"
#include "BLI_math_vector.hh"
#include "BLI_task.h"
#include "BLI_task.hh"

#include "editors/sculpt_paint/mesh_brush_common.hh"
#include "editors/sculpt_paint/paint_intern.hh"
#include "editors/sculpt_paint/paint_mask.hh"
#include "editors/sculpt_paint/sculpt_automask.hh"
#include "editors/sculpt_paint/sculpt_intern.hh"

namespace blender::ed::sculpt_paint {

inline namespace layer_cc {

struct LocalData {
  Vector<float3> positions;
  Vector<float> factors;
  Vector<float> distances;
  Vector<float> masks;
  Vector<float> displacement_factors;
  Vector<float3> translations;
};

BLI_NOINLINE static void offset_displacement_factors(const MutableSpan<float> displacement_factors,
                                                     const Span<float> factors,
                                                     const float strength)
{
  for (const int i : displacement_factors.index_range()) {
    displacement_factors[i] += factors[i] * strength * (1.05f - std::abs(displacement_factors[i]));
  }
}

/**
 * When using persistent base, the layer brush (holding Control) invert mode resets the
 * height of the layer to 0. This makes possible to clean edges of previously added layers
 * on top of the base.
 *
 * The main direction of the layers is inverted using the regular brush strength with the
 * brush direction property.
 */
BLI_NOINLINE static void reset_displacement_factors(const MutableSpan<float> displacement_factors,
                                                    const Span<float> factors,
                                                    const float strength)
{
  for (const int i : displacement_factors.index_range()) {
    displacement_factors[i] += std::abs(factors[i] * strength * displacement_factors[i]) *
                               (displacement_factors[i] > 0.0f ? -1.0f : 1.0f);
  }
}

BLI_NOINLINE static void clamp_displacement_factors(const MutableSpan<float> displacement_factors,
                                                    const Span<float> masks)
{
  if (masks.is_empty()) {
    for (const int i : displacement_factors.index_range()) {
      displacement_factors[i] = std::clamp(displacement_factors[i], -1.0f, 1.0f);
    }
  }
  else {
    for (const int i : displacement_factors.index_range()) {
      const float clamp_mask = 1.0f - masks[i];
      displacement_factors[i] = std::clamp(displacement_factors[i], -clamp_mask, clamp_mask);
    }
  }
}

BLI_NOINLINE static void calc_translations(const Span<float3> orig_positions,
                                           const Span<float3> orig_normals,
                                           const Span<float3> positions,
                                           const Span<float> displacement_factors,
                                           const Span<float> factors,
                                           const float height,
                                           const MutableSpan<float3> r_translations)
{
  for (const int i : positions.index_range()) {
    const float3 offset = orig_normals[i] * height * displacement_factors[i];
    const float3 translation = orig_positions[i] + offset - positions[i];
    r_translations[i] = translation * factors[i];
  }
}

BLI_NOINLINE static void calc_translations(const Span<float3> base_positions,
                                           const Span<float3> base_normals,
                                           const Span<int> verts,
                                           const Span<float3> positions,
                                           const Span<float> displacement_factors,
                                           const Span<float> factors,
                                           const float height,
                                           const MutableSpan<float3> r_translations)
{
  for (const int i : positions.index_range()) {
    const float3 offset = base_normals[verts[i]] * height * displacement_factors[i];
    const float3 translation = base_positions[verts[i]] + offset - positions[i];
    r_translations[i] = translation * factors[i];
  }
}

static void calc_faces(const Depsgraph &depsgraph,
                       const Sculpt &sd,
                       const Brush &brush,
                       const MeshAttributeData &attribute_data,
                       const Span<float3> vert_normals,
                       const bool use_persistent_base,
                       const Span<float3> persistent_base_positions,
                       const Span<float3> persistent_base_normals,
                       Object &object,
                       bke::pbvh::MeshNode &node,
                       LocalData &tls,
                       MutableSpan<float> layer_displacement_factor,
                       const PositionDeformData &position_data)
{
  const SculptSession &ss = *object.sculpt;
  const StrokeCache &cache = *ss.cache;

  const Span<int> verts = node.verts();
  const MutableSpan positions = gather_data_mesh(position_data.eval, verts, tls.positions);

  calc_factors_common_mesh(depsgraph,
                           brush,
                           object,
                           attribute_data,
                           positions,
                           vert_normals,
                           node,
                           tls.factors,
                           tls.distances);

  if (attribute_data.mask.is_empty()) {
    tls.masks.clear();
  }
  else {
    tls.masks.resize(verts.size());
    gather_data_mesh(attribute_data.mask, verts, tls.masks.as_mutable_span());
  }
  const MutableSpan<float> masks = tls.masks;

  tls.displacement_factors.resize(verts.size());
  const MutableSpan<float> displacement_factors = tls.displacement_factors;
  gather_data_mesh(layer_displacement_factor.as_span(), verts, displacement_factors);

  if (use_persistent_base) {
    if (cache.invert) {
      reset_displacement_factors(displacement_factors, tls.factors, cache.bstrength);
    }
    else {
      offset_displacement_factors(displacement_factors, tls.factors, cache.bstrength);
    }
    clamp_displacement_factors(displacement_factors, masks);

    scatter_data_mesh(displacement_factors.as_span(), verts, layer_displacement_factor);

    tls.translations.resize(verts.size());
    const MutableSpan<float3> translations = tls.translations;
    calc_translations(persistent_base_positions,
                      persistent_base_normals,
                      verts,
                      positions,
                      displacement_factors,
                      tls.factors,
                      brush.height,
                      translations);

    clip_and_lock_translations(sd, ss, position_data.eval, verts, translations);
    position_data.deform(translations, verts);
  }
  else {
    offset_displacement_factors(displacement_factors, tls.factors, cache.bstrength);
    clamp_displacement_factors(displacement_factors, masks);

    scatter_data_mesh(displacement_factors.as_span(), verts, layer_displacement_factor);

    const OrigPositionData orig_data = orig_position_data_get_mesh(object, node);

    tls.translations.resize(verts.size());
    const MutableSpan<float3> translations = tls.translations;
    calc_translations(orig_data.positions,
                      orig_data.normals,
                      positions,
                      displacement_factors,
                      tls.factors,
                      brush.height,
                      translations);

    clip_and_lock_translations(sd, ss, position_data.eval, verts, translations);
    position_data.deform(translations, verts);
  }
}

static void calc_grids(const Depsgraph &depsgraph,
                       const Sculpt &sd,
                       const Brush &brush,
                       Object &object,
                       bke::pbvh::GridsNode &node,
                       LocalData &tls,
                       MutableSpan<float> layer_displacement_factor)
{
  SculptSession &ss = *object.sculpt;
  const StrokeCache &cache = *ss.cache;
  SubdivCCG &subdiv_ccg = *ss.subdiv_ccg;
  const CCGKey key = BKE_subdiv_ccg_key_top_level(subdiv_ccg);

  const Span<int> grids = node.grids();
  const MutableSpan positions = gather_grids_positions(subdiv_ccg, grids, tls.positions);

  calc_factors_common_grids(depsgraph, brush, object, positions, node, tls.factors, tls.distances);

  const MutableSpan<float> displacement_factors = gather_data_grids(
      subdiv_ccg, layer_displacement_factor.as_span(), grids, tls.displacement_factors);

  offset_displacement_factors(displacement_factors, tls.factors, cache.bstrength);
  if (key.has_mask) {
    tls.masks.resize(positions.size());
    mask::gather_mask_grids(subdiv_ccg, grids, tls.masks);
  }
  else {
    tls.masks.clear();
  }
  clamp_displacement_factors(displacement_factors, tls.masks);

  scatter_data_grids(subdiv_ccg, displacement_factors.as_span(), grids, layer_displacement_factor);

  const OrigPositionData orig_data = orig_position_data_get_grids(object, node);

  tls.translations.resize(positions.size());
  const MutableSpan<float3> translations = tls.translations;
  calc_translations(orig_data.positions,
                    orig_data.normals,
                    positions,
                    displacement_factors,
                    tls.factors,
                    brush.height,
                    translations);

  clip_and_lock_translations(sd, ss, positions, translations);
  apply_translations(translations, grids, subdiv_ccg);
}

static void calc_bmesh(const Depsgraph &depsgraph,
                       const Sculpt &sd,
                       const Brush &brush,
                       Object &object,
                       bke::pbvh::BMeshNode &node,
                       LocalData &tls,
                       MutableSpan<float> layer_displacement_factor)
{
  SculptSession &ss = *object.sculpt;
  const StrokeCache &cache = *ss.cache;

  const Set<BMVert *, 0> &verts = BKE_pbvh_bmesh_node_unique_verts(&node);

  const MutableSpan positions = gather_bmesh_positions(verts, tls.positions);

  calc_factors_common_bmesh(depsgraph, brush, object, positions, node, tls.factors, tls.distances);

  const MutableSpan<float> displacement_factors = gather_data_bmesh(
      layer_displacement_factor.as_span(), verts, tls.displacement_factors);

  offset_displacement_factors(displacement_factors, tls.factors, cache.bstrength);

  tls.masks.resize(verts.size());
  const MutableSpan<float> masks = tls.masks;
  mask::gather_mask_bmesh(*ss.bm, verts, masks);
  clamp_displacement_factors(displacement_factors, masks);

  scatter_data_bmesh(displacement_factors.as_span(), verts, layer_displacement_factor);

  Array<float3> orig_positions(verts.size());
  Array<float3> orig_normals(verts.size());
  orig_position_data_gather_bmesh(*ss.bm_log, verts, orig_positions, orig_normals);

  tls.translations.resize(verts.size());
  const MutableSpan<float3> translations = tls.translations;
  calc_translations(orig_positions,
                    orig_normals,
                    positions,
                    displacement_factors,
                    tls.factors,
                    brush.height,
                    translations);

  clip_and_lock_translations(sd, ss, positions, translations);
  apply_translations(translations, verts);
}

}  // namespace layer_cc

void do_layer_brush(const Depsgraph &depsgraph,
                    const Sculpt &sd,
                    Object &object,
                    const IndexMask &node_mask)
{
  SculptSession &ss = *object.sculpt;
  bke::pbvh::Tree &pbvh = *bke::object::pbvh_get(object);
  const Brush &brush = *BKE_paint_brush_for_read(&sd.paint);

  threading::EnumerableThreadSpecific<LocalData> all_tls;
  switch (pbvh.type()) {
    case bke::pbvh::Type::Mesh: {
      Mesh &mesh = *static_cast<Mesh *>(object.data);
      const PositionDeformData position_data(depsgraph, object);
      const Span<float3> vert_normals = bke::pbvh::vert_normals_eval(depsgraph, object);

      bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
      const MeshAttributeData attribute_data(attributes);
      const VArraySpan persistent_position = *attributes.lookup<float3>(".sculpt_persistent_co",
                                                                        bke::AttrDomain::Point);
      const VArraySpan persistent_normal = *attributes.lookup<float3>(".sculpt_persistent_no",
                                                                      bke::AttrDomain::Point);

      bke::SpanAttributeWriter<float> persistent_disp_attr;
      bool use_persistent_base = false;
      MutableSpan<float> displacement;
      if (brush.flag & BRUSH_PERSISTENT) {
        if (!persistent_position.is_empty() && !persistent_normal.is_empty()) {
          persistent_disp_attr = attributes.lookup_or_add_for_write_span<float>(
              ".sculpt_persistent_disp", bke::AttrDomain::Point);
          if (persistent_disp_attr) {
            use_persistent_base = true;
            displacement = persistent_disp_attr.span;
          }
        }
      }

      if (displacement.is_empty()) {
        if (ss.cache->layer_displacement_factor.is_empty()) {
          ss.cache->layer_displacement_factor = Array<float>(SCULPT_vertex_count_get(object),
                                                             0.0f);
        }
        displacement = ss.cache->layer_displacement_factor;
      }

      MutableSpan<bke::pbvh::MeshNode> nodes = pbvh.nodes<bke::pbvh::MeshNode>();
      node_mask.foreach_index(GrainSize(1), [&](const int i) {
        LocalData &tls = all_tls.local();
        calc_faces(depsgraph,
                   sd,
                   brush,
                   attribute_data,
                   vert_normals,
                   use_persistent_base,
                   persistent_position,
                   persistent_normal,
                   object,
                   nodes[i],
                   tls,
                   displacement,
                   position_data);
        bke::pbvh::update_node_bounds_mesh(position_data.eval, nodes[i]);
      });
      persistent_disp_attr.finish();
      break;
    }
    case bke::pbvh::Type::Grids: {
      SubdivCCG &subdiv_ccg = *object.sculpt->subdiv_ccg;
      MutableSpan<float3> positions = subdiv_ccg.positions;
      if (ss.cache->layer_displacement_factor.is_empty()) {
        ss.cache->layer_displacement_factor = Array<float>(positions.size(), 0.0f);
      }
      const MutableSpan<float> displacement = ss.cache->layer_displacement_factor;
      MutableSpan<bke::pbvh::GridsNode> nodes = pbvh.nodes<bke::pbvh::GridsNode>();
      node_mask.foreach_index(GrainSize(1), [&](const int i) {
        LocalData &tls = all_tls.local();
        calc_grids(depsgraph, sd, brush, object, nodes[i], tls, displacement);
        bke::pbvh::update_node_bounds_grids(subdiv_ccg.grid_area, positions, nodes[i]);
      });
      break;
    }
    case bke::pbvh::Type::BMesh: {
      if (ss.cache->layer_displacement_factor.is_empty()) {
        ss.cache->layer_displacement_factor = Array<float>(SCULPT_vertex_count_get(object), 0.0f);
      }
      const MutableSpan<float> displacement = ss.cache->layer_displacement_factor;
      MutableSpan<bke::pbvh::BMeshNode> nodes = pbvh.nodes<bke::pbvh::BMeshNode>();
      node_mask.foreach_index(GrainSize(1), [&](const int i) {
        LocalData &tls = all_tls.local();
        calc_bmesh(depsgraph, sd, brush, object, nodes[i], tls, displacement);
        bke::pbvh::update_node_bounds_bmesh(nodes[i]);
      });
      break;
    }
  }
  pbvh.tag_positions_changed(node_mask);
  bke::pbvh::flush_bounds_to_parents(pbvh);
}

}  // namespace blender::ed::sculpt_paint