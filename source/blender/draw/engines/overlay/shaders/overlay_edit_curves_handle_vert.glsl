/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "common_view_clipping_lib.glsl"
#include "common_view_lib.glsl"

void main()
{
  GPU_INTEL_VERTEX_SHADER_WORKAROUND

  vec3 world_pos = point_object_to_world(pos);
  gl_Position = point_world_to_ndc(world_pos);
  vert.flag = data;
  vert.selection = selection;

  view_clipping_distances(world_pos);
}
