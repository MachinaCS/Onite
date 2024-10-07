/* SPDX-FileCopyrightText: 2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gpu
 */

#include "gpu_shader_create_info.hh"

GPU_SHADER_INTERFACE_INFO(smooth_uv_iface)
SMOOTH(VEC2, uv)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_CREATE_INFO(gpu_shader_2D_area_borders)
VERTEX_IN(0, VEC2, pos)
VERTEX_OUT(smooth_uv_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
PUSH_CONSTANT(MAT4, ModelViewProjectionMatrix)
PUSH_CONSTANT(VEC4, rect)
PUSH_CONSTANT(VEC4, color)
PUSH_CONSTANT(FLOAT, scale)
PUSH_CONSTANT(INT, cornerLen)
VERTEX_SOURCE("gpu_shader_2D_area_borders_vert.glsl")
FRAGMENT_SOURCE("gpu_shader_2D_area_borders_frag.glsl")
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
