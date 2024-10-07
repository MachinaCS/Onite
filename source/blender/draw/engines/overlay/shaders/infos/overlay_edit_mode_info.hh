/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_create_info.hh"

GPU_SHADER_INTERFACE_INFO(overlay_edit_flat_color_iface)
FLAT(VEC4, finalColor)
GPU_SHADER_INTERFACE_END()
GPU_SHADER_INTERFACE_INFO(overlay_edit_smooth_color_iface)
SMOOTH(VEC4, finalColor)
GPU_SHADER_INTERFACE_END()
GPU_SHADER_INTERFACE_INFO(overlay_edit_nopersp_color_iface)
NO_PERSPECTIVE(VEC4, finalColor)
GPU_SHADER_INTERFACE_END()

/* -------------------------------------------------------------------- */
/** \name Edit Mesh
 * \{ */

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_common)
DEFINE_VALUE("blender_srgb_to_framebuffer_space(a)", "a")
SAMPLER(0, DEPTH_2D, depthTex)
FRAGMENT_OUT(0, VEC4, fragColor)
PUSH_CONSTANT(BOOL, wireShading)
PUSH_CONSTANT(BOOL, selectFace)
PUSH_CONSTANT(BOOL, selectEdge)
PUSH_CONSTANT(FLOAT, alpha)
PUSH_CONSTANT(FLOAT, retopologyOffset)
PUSH_CONSTANT(IVEC4, dataMask)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(overlay_edit_mesh_common_no_geom)
METAL_BACKEND_ONLY()
DEFINE_VALUE("blender_srgb_to_framebuffer_space(a)", "a")
SAMPLER(0, DEPTH_2D, depthTex)
FRAGMENT_OUT(0, VEC4, fragColor)
PUSH_CONSTANT(BOOL, wireShading)
PUSH_CONSTANT(BOOL, selectFace)
PUSH_CONSTANT(BOOL, selectEdge)
PUSH_CONSTANT(FLOAT, alpha)
PUSH_CONSTANT(FLOAT, retopologyOffset)
PUSH_CONSTANT(IVEC4, dataMask)
VERTEX_SOURCE("overlay_edit_mesh_vert_no_geom.glsl")
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()
#endif

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_depth)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
PUSH_CONSTANT(FLOAT, retopologyOffset)
VERTEX_SOURCE("overlay_edit_mesh_depth_vert.glsl")
FRAGMENT_SOURCE("overlay_depth_only_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_depth_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_mesh_depth)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_INTERFACE_INFO(overlay_edit_mesh_vert_iface)
SMOOTH(VEC4, finalColor)
SMOOTH(FLOAT, vertexCrease)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_vert)
DO_STATIC_COMPILATION()
BUILTINS(BuiltinBits::POINT_SIZE)
DEFINE("VERT")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UVEC4, data)
VERTEX_IN(2, VEC3, vnor)
VERTEX_SOURCE("overlay_edit_mesh_vert.glsl")
VERTEX_OUT(overlay_edit_mesh_vert_iface)
FRAGMENT_SOURCE("overlay_point_varying_color_frag.glsl")
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(overlay_edit_mesh_common)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_vert_next)
DO_STATIC_COMPILATION()
BUILTINS(BuiltinBits::POINT_SIZE)
DEFINE("VERT")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UVEC4, data)
VERTEX_IN(2, VEC3, vnor)
VERTEX_SOURCE("overlay_edit_mesh_vertex_vert.glsl")
VERTEX_OUT(overlay_edit_mesh_vert_iface)
FRAGMENT_SOURCE("overlay_point_varying_color_frag.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
ADDITIONAL_INFO(overlay_edit_mesh_common)
GPU_SHADER_CREATE_END()

GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_mesh_edge_iface, geometry_in)
SMOOTH(VEC4, finalColor_)
SMOOTH(VEC4, finalColorOuter_)
SMOOTH(UINT, selectOverride_)
GPU_SHADER_NAMED_INTERFACE_END(geometry_in)

GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_mesh_edge_geom_iface, geometry_out)
SMOOTH(VEC4, finalColor)
GPU_SHADER_NAMED_INTERFACE_END(geometry_out)
GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_mesh_edge_geom_flat_iface, geometry_flat_out)
FLAT(VEC4, finalColorOuter)
GPU_SHADER_NAMED_INTERFACE_END(geometry_flat_out)
GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_mesh_edge_geom_noperspective_iface,
                                geometry_noperspective_out)
NO_PERSPECTIVE(FLOAT, edgeCoord)
GPU_SHADER_NAMED_INTERFACE_END(geometry_noperspective_out)

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_edge)
DO_STATIC_COMPILATION()
DEFINE("EDGE")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UVEC4, data)
VERTEX_IN(2, VEC3, vnor)
PUSH_CONSTANT(BOOL, do_smooth_wire)
VERTEX_SOURCE("overlay_edit_mesh_vert.glsl")
VERTEX_OUT(overlay_edit_mesh_edge_iface)
GEOMETRY_OUT(overlay_edit_mesh_edge_geom_iface)
GEOMETRY_OUT(overlay_edit_mesh_edge_geom_flat_iface)
GEOMETRY_OUT(overlay_edit_mesh_edge_geom_noperspective_iface)
GEOMETRY_LAYOUT(PrimitiveIn::LINES, PrimitiveOut::TRIANGLE_STRIP, 4)
GEOMETRY_SOURCE("overlay_edit_mesh_geom.glsl")
FRAGMENT_SOURCE("overlay_edit_mesh_frag.glsl")
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(overlay_edit_mesh_common)
GPU_SHADER_CREATE_END()

/* The Non-Geometry shader variant passes directly to fragment. */
#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(overlay_edit_mesh_edge_no_geom)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
DEFINE("EDGE")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UCHAR4, data)
VERTEX_IN(2, VEC3_101010I2, vnor)
PUSH_CONSTANT(BOOL, do_smooth_wire)
VERTEX_OUT(overlay_edit_mesh_edge_geom_iface)
VERTEX_OUT(overlay_edit_mesh_edge_geom_flat_iface)
VERTEX_OUT(overlay_edit_mesh_edge_geom_noperspective_iface)
FRAGMENT_SOURCE("overlay_edit_mesh_frag.glsl")
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(overlay_edit_mesh_common_no_geom)
GPU_SHADER_CREATE_END()
#endif

/* Vertex Pull version for overlay next. */
GPU_SHADER_CREATE_INFO(overlay_edit_mesh_edge_next)
DO_STATIC_COMPILATION()
DEFINE("EDGE")
STORAGE_BUF_FREQ(0, READ, float, pos[], GEOMETRY)
STORAGE_BUF_FREQ(1, READ, uint, vnor[], GEOMETRY)
STORAGE_BUF_FREQ(2, READ, uint, data[], GEOMETRY)
PUSH_CONSTANT(IVEC2, gpu_attr_0)
PUSH_CONSTANT(IVEC2, gpu_attr_1)
PUSH_CONSTANT(IVEC2, gpu_attr_2)
PUSH_CONSTANT(BOOL, do_smooth_wire)
PUSH_CONSTANT(BOOL, use_vertex_selection)
VERTEX_OUT(overlay_edit_mesh_edge_geom_iface)
VERTEX_OUT(overlay_edit_mesh_edge_geom_flat_iface)
VERTEX_OUT(overlay_edit_mesh_edge_geom_noperspective_iface)
VERTEX_SOURCE("overlay_edit_mesh_edge_vert.glsl")
FRAGMENT_SOURCE("overlay_edit_mesh_frag.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
ADDITIONAL_INFO(gpu_index_load)
ADDITIONAL_INFO(overlay_edit_mesh_common)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_edge_flat)
DO_STATIC_COMPILATION()
DEFINE("FLAT")
ADDITIONAL_INFO(overlay_edit_mesh_edge)
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(overlay_edit_mesh_edge_flat_no_geom)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
DEFINE("FLAT")
ADDITIONAL_INFO(overlay_edit_mesh_edge_no_geom)
GPU_SHADER_CREATE_END()
#endif

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_face)
DO_STATIC_COMPILATION()
DEFINE("FACE")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UVEC4, data)
VERTEX_SOURCE("overlay_edit_mesh_vert.glsl")
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(overlay_edit_mesh_common)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_face_next)
DO_STATIC_COMPILATION()
DEFINE("FACE")
DEFINE_VALUE("vnor", "vec3(0.0)")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UVEC4, data)
VERTEX_SOURCE("overlay_edit_mesh_face_vert.glsl")
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
ADDITIONAL_INFO(overlay_edit_mesh_common)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_facedot)
DO_STATIC_COMPILATION()
DEFINE("FACEDOT")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UVEC4, data)
VERTEX_IN(2, VEC4, norAndFlag)
DEFINE_VALUE("vnor", "norAndFlag.xyz")
VERTEX_SOURCE("overlay_edit_mesh_vert.glsl")
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_SOURCE("overlay_point_varying_color_frag.glsl")
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(overlay_edit_mesh_common)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_facedot_next)
DO_STATIC_COMPILATION()
DEFINE("FACEDOT")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UVEC4, data)
VERTEX_IN(2, VEC4, norAndFlag)
DEFINE_VALUE("vnor", "norAndFlag.xyz")
VERTEX_SOURCE("overlay_edit_mesh_facedot_vert.glsl")
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_SOURCE("overlay_point_varying_color_frag.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
ADDITIONAL_INFO(overlay_edit_mesh_common)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_normal)
DO_STATIC_COMPILATION()
DEFINE("WORKAROUND_INDEX_LOAD_INCLUDE")
/* WORKAROUND: Needed to support OpenSubdiv vertex format. Should be removed. */
PUSH_CONSTANT(IVEC2, gpu_attr_0)
PUSH_CONSTANT(IVEC2, gpu_attr_1)
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, VEC4, lnor)
VERTEX_IN(2, VEC4, vnor)
VERTEX_IN(3, VEC4, norAndFlag)
SAMPLER(0, DEPTH_2D, depthTex)
PUSH_CONSTANT(FLOAT, normalSize)
PUSH_CONSTANT(FLOAT, normalScreenSize)
PUSH_CONSTANT(FLOAT, alpha)
PUSH_CONSTANT(BOOL, isConstantScreenSizeNormals)
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_mesh_normal_vert.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_modelmat_instanced_attr)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_INTERFACE_INFO(overlay_edit_mesh_analysis_iface)
SMOOTH(VEC4, weightColor)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_analysis)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, FLOAT, weight)
SAMPLER(0, FLOAT_1D, weightTex)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_OUT(overlay_edit_mesh_analysis_iface)
VERTEX_SOURCE("overlay_edit_mesh_analysis_vert.glsl")
FRAGMENT_SOURCE("overlay_edit_mesh_analysis_frag.glsl")
ADDITIONAL_INFO(draw_modelmat)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_skin_root)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, FLOAT, size)
VERTEX_IN(2, VEC3, local_pos)
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_mesh_skin_root_vert.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_modelmat_instanced_attr)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_vert_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_mesh_vert)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_edge_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_mesh_edge)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(overlay_edit_mesh_edge_clipped_no_geom)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_mesh_edge_no_geom)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()
#endif

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_edge_flat_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_mesh_edge_flat)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(overlay_edit_mesh_edge_flat_clipped_no_geom)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_mesh_edge_flat_no_geom)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()
#endif

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_face_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_mesh_face)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_facedot_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_mesh_facedot)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_normal_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_mesh_normal)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_analysis_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_mesh_analysis)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_mesh_skin_root_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_mesh_skin_root)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit UV
 * \{ */

GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_uv_iface, geom_in)
SMOOTH(FLOAT, selectionFac)
GPU_SHADER_NAMED_INTERFACE_END(geom_in)
GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_uv_flat_iface, geom_flat_in)
FLAT(VEC2, stippleStart)
GPU_SHADER_NAMED_INTERFACE_END(geom_flat_in)
GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_uv_noperspective_iface, geom_noperspective_in)
NO_PERSPECTIVE(VEC2, stipplePos)
GPU_SHADER_NAMED_INTERFACE_END(geom_noperspective_in)

GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_uv_geom_iface, geom_out)
SMOOTH(FLOAT, selectionFac)
GPU_SHADER_NAMED_INTERFACE_END(geom_out)
GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_uv_geom_flat_iface, geom_flat_out)
FLAT(VEC2, stippleStart)
GPU_SHADER_NAMED_INTERFACE_END(geom_flat_out)
GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_uv_geom_noperspective_iface, geom_noperspective_out)
NO_PERSPECTIVE(FLOAT, edgeCoord)
NO_PERSPECTIVE(VEC2, stipplePos)
GPU_SHADER_NAMED_INTERFACE_END(geom_noperspective_out)

GPU_SHADER_CREATE_INFO(overlay_edit_uv_edges_common)
VERTEX_IN(0, VEC2, au)
VERTEX_IN(1, INT, flag)
PUSH_CONSTANT(INT, lineStyle)
PUSH_CONSTANT(BOOL, doSmoothWire)
PUSH_CONSTANT(FLOAT, alpha)
PUSH_CONSTANT(FLOAT, dashLength)
FRAGMENT_OUT(0, VEC4, fragColor)
FRAGMENT_SOURCE("overlay_edit_uv_edges_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_uv_edges)
ADDITIONAL_INFO(overlay_edit_uv_edges_common)
DO_STATIC_COMPILATION()
VERTEX_OUT(overlay_edit_uv_iface)
VERTEX_OUT(overlay_edit_uv_flat_iface)
VERTEX_OUT(overlay_edit_uv_noperspective_iface)
GEOMETRY_LAYOUT(PrimitiveIn::LINES, PrimitiveOut::TRIANGLE_STRIP, 4)
GEOMETRY_OUT(overlay_edit_uv_geom_iface)
GEOMETRY_OUT(overlay_edit_uv_geom_flat_iface)
GEOMETRY_OUT(overlay_edit_uv_geom_noperspective_iface)
VERTEX_SOURCE("overlay_edit_uv_edges_vert.glsl")
GEOMETRY_SOURCE("overlay_edit_uv_edges_geom.glsl")
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(overlay_edit_uv_edges_no_geom)
METAL_BACKEND_ONLY()
ADDITIONAL_INFO(overlay_edit_uv_edges_common)
DO_STATIC_COMPILATION()
VERTEX_OUT(overlay_edit_uv_geom_iface)
VERTEX_OUT(overlay_edit_uv_geom_flat_iface)
VERTEX_OUT(overlay_edit_uv_geom_noperspective_iface)
VERTEX_SOURCE("overlay_edit_uv_edges_vert_no_geom.glsl")
GPU_SHADER_CREATE_END()
#endif

GPU_SHADER_CREATE_INFO(overlay_edit_uv_edges_select)
DO_STATIC_COMPILATION()
DEFINE("USE_EDGE_SELECT")
ADDITIONAL_INFO(overlay_edit_uv_edges)
GPU_SHADER_CREATE_END()

GPU_SHADER_INTERFACE_INFO(overlay_edit_uv_next_iface)
SMOOTH(FLOAT, selectionFac)
FLAT(VEC2, stippleStart)
NO_PERSPECTIVE(FLOAT, edgeCoord)
NO_PERSPECTIVE(VEC2, stipplePos)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_uv_edges_next)
DO_STATIC_COMPILATION()
STORAGE_BUF_FREQ(0, READ, float, au[], GEOMETRY)
STORAGE_BUF_FREQ(1, READ, uint, data[], GEOMETRY)
PUSH_CONSTANT(IVEC2, gpu_attr_0)
PUSH_CONSTANT(IVEC2, gpu_attr_1)
PUSH_CONSTANT(INT, lineStyle)
PUSH_CONSTANT(BOOL, doSmoothWire)
PUSH_CONSTANT(FLOAT, alpha)
PUSH_CONSTANT(FLOAT, dashLength)
SPECIALIZATION_CONSTANT(BOOL, use_edge_select, false)
VERTEX_OUT(overlay_edit_uv_next_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_uv_edges_next_vert.glsl")
FRAGMENT_SOURCE("overlay_edit_uv_edges_next_frag.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
ADDITIONAL_INFO(gpu_index_load)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_uv_faces)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC2, au)
VERTEX_IN(1, UINT, flag)
PUSH_CONSTANT(FLOAT, uvOpacity)
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_uv_faces_vert.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_uv_face_dots)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC2, au)
VERTEX_IN(1, UINT, flag)
PUSH_CONSTANT(FLOAT, pointSize)
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_uv_face_dots_vert.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_INTERFACE_INFO(overlay_edit_uv_vert_iface)
SMOOTH(VEC4, fillColor)
SMOOTH(VEC4, outlineColor)
SMOOTH(VEC4, radii)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_uv_verts)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC2, au)
VERTEX_IN(1, UINT, flag)
PUSH_CONSTANT(FLOAT, pointSize)
PUSH_CONSTANT(FLOAT, outlineWidth)
PUSH_CONSTANT(VEC4, color)
VERTEX_OUT(overlay_edit_uv_vert_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_uv_verts_vert.glsl")
FRAGMENT_SOURCE("overlay_edit_uv_verts_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_uv_tiled_image_borders)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
PUSH_CONSTANT(VEC4, ucolor)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_uv_tiled_image_borders_vert.glsl")
FRAGMENT_SOURCE("overlay_uniform_color_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
GPU_SHADER_CREATE_END()

GPU_SHADER_INTERFACE_INFO(edit_uv_image_iface)
SMOOTH(VEC2, uvs)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_uv_stencil_image)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_OUT(edit_uv_image_iface)
VERTEX_SOURCE("overlay_edit_uv_image_vert.glsl")
SAMPLER(0, FLOAT_2D, imgTexture)
PUSH_CONSTANT(BOOL, imgPremultiplied)
PUSH_CONSTANT(BOOL, imgAlphaBlend)
PUSH_CONSTANT(VEC4, ucolor)
FRAGMENT_OUT(0, VEC4, fragColor)
FRAGMENT_SOURCE("overlay_image_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_uv_mask_image)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_OUT(edit_uv_image_iface)
SAMPLER(0, FLOAT_2D, imgTexture)
PUSH_CONSTANT(VEC4, color)
PUSH_CONSTANT(FLOAT, opacity)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_uv_image_vert.glsl")
FRAGMENT_SOURCE("overlay_edit_uv_image_mask_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name UV Stretching
 * \{ */

GPU_SHADER_CREATE_INFO(overlay_edit_uv_stretching)
VERTEX_IN(0, VEC2, pos)
PUSH_CONSTANT(VEC2, aspect)
PUSH_CONSTANT(FLOAT, stretch_opacity)
VERTEX_OUT(overlay_edit_nopersp_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_uv_stretching_vert.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
PUSH_CONSTANT(FLOAT, totalAreaRatio)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_uv_stretching_area)
DO_STATIC_COMPILATION()
VERTEX_IN(1, FLOAT, ratio)
PUSH_CONSTANT(FLOAT, totalAreaRatio)
ADDITIONAL_INFO(overlay_edit_uv_stretching)
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_uv_stretching_angle)
DO_STATIC_COMPILATION()
DEFINE("STRETCH_ANGLE")
VERTEX_IN(1, VEC2, uv_angles)
VERTEX_IN(2, FLOAT, angle)
ADDITIONAL_INFO(overlay_edit_uv_stretching)
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit Curve
 * \{ */

GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_curve_handle_iface, vert)
FLAT(UINT, flag)
GPU_SHADER_NAMED_INTERFACE_END(vert)

GPU_SHADER_CREATE_INFO(overlay_edit_curve_handle)
DO_STATIC_COMPILATION()
TYPEDEF_SOURCE("overlay_shader_shared.h")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UINT, data)
VERTEX_OUT(overlay_edit_curve_handle_iface)
GEOMETRY_LAYOUT(PrimitiveIn::LINES, PrimitiveOut::TRIANGLE_STRIP, 10)
GEOMETRY_OUT(overlay_edit_smooth_color_iface)
PUSH_CONSTANT(BOOL, showCurveHandles)
PUSH_CONSTANT(INT, curveHandleDisplay)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_curve_handle_vert.glsl")
GEOMETRY_SOURCE("overlay_edit_curve_handle_geom.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(overlay_edit_curve_handle_no_geom)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
TYPEDEF_SOURCE("overlay_shader_shared.h")
/* NOTE: Color already in Linear space. Which is what we want. */
DEFINE_VALUE("srgbTarget", "false")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UINT, data)
VERTEX_OUT(overlay_edit_smooth_color_iface)
PUSH_CONSTANT(BOOL, showCurveHandles)
PUSH_CONSTANT(INT, curveHandleDisplay)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_curve_handle_vert_no_geom.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()
#endif

GPU_SHADER_CREATE_INFO(overlay_edit_curve_handle_next)
DO_STATIC_COMPILATION()
TYPEDEF_SOURCE("overlay_shader_shared.h")
STORAGE_BUF_FREQ(0, READ, float, pos[], GEOMETRY)
STORAGE_BUF_FREQ(1, READ, uint, data[], GEOMETRY)
PUSH_CONSTANT(IVEC2, gpu_attr_0)
PUSH_CONSTANT(IVEC2, gpu_attr_1)
VERTEX_OUT(overlay_edit_smooth_color_iface)
PUSH_CONSTANT(BOOL, showCurveHandles)
PUSH_CONSTANT(INT, curveHandleDisplay)
PUSH_CONSTANT(FLOAT, alpha)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_curve_handle_next_vert.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
ADDITIONAL_INFO(gpu_index_load)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_curves_handle_next)
DO_STATIC_COMPILATION()
TYPEDEF_SOURCE("overlay_shader_shared.h")
STORAGE_BUF_FREQ(0, READ, float, pos[], GEOMETRY)
STORAGE_BUF_FREQ(1, READ, uint, data[], GEOMETRY)
STORAGE_BUF_FREQ(2, READ, float, selection[], GEOMETRY)
PUSH_CONSTANT(IVEC2, gpu_attr_0)
PUSH_CONSTANT(IVEC2, gpu_attr_1)
PUSH_CONSTANT(IVEC2, gpu_attr_2)
VERTEX_OUT(overlay_edit_smooth_color_iface)
PUSH_CONSTANT(BOOL, showCurveHandles)
PUSH_CONSTANT(INT, curveHandleDisplay)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_curves_handle_next_vert.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
ADDITIONAL_INFO(gpu_index_load)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_curve_handle_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_curve_handle)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(overlay_edit_curve_handle_clipped_no_geom)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_curve_handle_no_geom)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()
#endif

GPU_SHADER_CREATE_INFO(overlay_edit_curve_point)
DO_STATIC_COMPILATION()
TYPEDEF_SOURCE("overlay_shader_shared.h")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UINT, data)
VERTEX_OUT(overlay_edit_flat_color_iface)
PUSH_CONSTANT(BOOL, showCurveHandles)
PUSH_CONSTANT(INT, curveHandleDisplay)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_curve_point_vert.glsl")
FRAGMENT_SOURCE("overlay_point_varying_color_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_curve_point_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_curve_point)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_curve_wire)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, VEC3, nor)
VERTEX_IN(2, VEC3, tan)
VERTEX_IN(3, FLOAT, rad)
PUSH_CONSTANT(FLOAT, normalSize)
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_curve_wire_vert.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_resource_id_uniform)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_curve_wire_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_curve_wire)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_curve_normals)
DO_STATIC_COMPILATION()
STORAGE_BUF_FREQ(0, READ, float, pos[], GEOMETRY)
STORAGE_BUF_FREQ(1, READ, float, rad[], GEOMETRY)
STORAGE_BUF_FREQ(2, READ, uint, nor[], GEOMETRY)
STORAGE_BUF_FREQ(3, READ, uint, tan[], GEOMETRY)
PUSH_CONSTANT(IVEC2, gpu_attr_0)
PUSH_CONSTANT(IVEC2, gpu_attr_1)
PUSH_CONSTANT(IVEC2, gpu_attr_2)
PUSH_CONSTANT(IVEC2, gpu_attr_3)
PUSH_CONSTANT(FLOAT, normalSize)
PUSH_CONSTANT(BOOL, use_hq_normals)
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_curve_wire_next_vert.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
ADDITIONAL_INFO(gpu_index_load)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit Curves
 * \{ */

GPU_SHADER_NAMED_INTERFACE_INFO(overlay_edit_curves_handle_iface, vert)
FLAT(UINT, flag)
FLAT(FLOAT, selection)
GPU_SHADER_NAMED_INTERFACE_END(vert)

GPU_SHADER_CREATE_INFO(overlay_edit_curves_handle)
DO_STATIC_COMPILATION()
TYPEDEF_SOURCE("overlay_shader_shared.h")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UINT, data)
VERTEX_IN(2, FLOAT, selection)
VERTEX_OUT(overlay_edit_curves_handle_iface)
GEOMETRY_LAYOUT(PrimitiveIn::LINES, PrimitiveOut::TRIANGLE_STRIP, 10)
GEOMETRY_OUT(overlay_edit_smooth_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_curves_handle_vert.glsl")
GEOMETRY_SOURCE("overlay_edit_curves_handle_geom.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_curves_handle_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_curves_handle)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

#ifdef WITH_METAL_BACKEND
GPU_SHADER_CREATE_INFO(overlay_edit_curves_handle_no_geom)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
TYPEDEF_SOURCE("overlay_shader_shared.h")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UINT, data)
VERTEX_IN(2, FLOAT, selection)
VERTEX_OUT(overlay_edit_smooth_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_curves_handle_vert_no_geom.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_curves_handle_clipped_no_geom)
METAL_BACKEND_ONLY()
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_curves_handle_no_geom)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()
#endif

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit Lattice
 * \{ */

GPU_SHADER_CREATE_INFO(overlay_edit_lattice_point)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UINT, data)
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_lattice_point_vert.glsl")
FRAGMENT_SOURCE("overlay_point_varying_color_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_lattice_point_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_lattice_point)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_lattice_wire)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, FLOAT, weight)
SAMPLER(0, FLOAT_1D, weightTex)
VERTEX_OUT(overlay_edit_smooth_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_lattice_wire_vert.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_lattice_wire_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_lattice_wire)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit Particle
 * \{ */

GPU_SHADER_CREATE_INFO(overlay_edit_particle_strand)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, FLOAT, selection)
SAMPLER(0, FLOAT_1D, weightTex)
PUSH_CONSTANT(BOOL, useWeight)
PUSH_CONSTANT(BOOL, useGreasePencil)
VERTEX_OUT(overlay_edit_smooth_color_iface)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_particle_strand_vert.glsl")
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_particle_strand_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_particle_strand)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_particle_point)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, FLOAT, selection)
VERTEX_OUT(overlay_edit_flat_color_iface)
SAMPLER(0, FLOAT_1D, weightTex)
PUSH_CONSTANT(BOOL, useWeight)
PUSH_CONSTANT(BOOL, useGreasePencil)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_particle_point_vert.glsl")
FRAGMENT_SOURCE("overlay_point_varying_color_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_particle_point_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_particle_point)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name Edit GPencil
 * \{ */

GPU_SHADER_CREATE_INFO(overlay_edit_gpencil)
TYPEDEF_SOURCE("overlay_shader_shared.h")
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, INT, ma)
VERTEX_IN(2, UINT, vflag)
VERTEX_IN(3, FLOAT, weight)
PUSH_CONSTANT(FLOAT, normalSize)
PUSH_CONSTANT(BOOL, doMultiframe)
PUSH_CONSTANT(BOOL, doStrokeEndpoints)
PUSH_CONSTANT(BOOL, hideSelect)
PUSH_CONSTANT(BOOL, doWeightColor)
PUSH_CONSTANT(FLOAT, gpEditOpacity)
PUSH_CONSTANT(VEC4, gpEditColor)
SAMPLER(0, FLOAT_1D, weightTex)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_gpencil_vert.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_gpencil_wire)
DO_STATIC_COMPILATION()
VERTEX_OUT(overlay_edit_smooth_color_iface)
FRAGMENT_SOURCE("overlay_varying_color.glsl")
ADDITIONAL_INFO(overlay_edit_gpencil)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_gpencil_wire_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_gpencil_wire)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_gpencil_point)
DO_STATIC_COMPILATION()
DEFINE("USE_POINTS")
VERTEX_OUT(overlay_edit_flat_color_iface)
FRAGMENT_SOURCE("overlay_point_varying_color_frag.glsl")
ADDITIONAL_INFO(overlay_edit_gpencil)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_gpencil_point_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_gpencil_point)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

/* TODO(fclem): Refactor this to take list of point instead of drawing 1 point per drawcall. */
GPU_SHADER_CREATE_INFO(overlay_edit_gpencil_guide_point)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_IN(1, UINT, data)
VERTEX_OUT(overlay_edit_flat_color_iface)
PUSH_CONSTANT(VEC3, pPosition)
PUSH_CONSTANT(FLOAT, pSize)
PUSH_CONSTANT(VEC4, pColor)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_edit_gpencil_guide_vert.glsl")
FRAGMENT_SOURCE("overlay_point_varying_color_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
ADDITIONAL_INFO(draw_globals)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_edit_gpencil_guide_point_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_edit_gpencil_guide_point)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name Depth Only Shader
 *
 * Used to occlude edit geometry which might not be rendered by the render engine.
 * \{ */

GPU_SHADER_CREATE_INFO(overlay_depth_only)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_SOURCE("overlay_depth_only_vert.glsl")
FRAGMENT_SOURCE("overlay_depth_only_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_depth_only_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_depth_only)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_depth_mesh)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
VERTEX_SOURCE("basic_depth_vert.glsl")
FRAGMENT_SOURCE("overlay_depth_only_frag.glsl")
ADDITIONAL_INFO(draw_globals)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_depth_mesh_conservative)
DO_STATIC_COMPILATION()
STORAGE_BUF_FREQ(0, READ, float, pos[], GEOMETRY)
PUSH_CONSTANT(IVEC2, gpu_attr_0)
VERTEX_SOURCE("overlay_depth_only_mesh_conservative_vert.glsl")
FRAGMENT_SOURCE("overlay_depth_only_frag.glsl")
ADDITIONAL_INFO(draw_globals)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(gpu_index_load)
ADDITIONAL_INFO(draw_resource_handle_new)
GPU_SHADER_CREATE_END()

GPU_SHADER_NAMED_INTERFACE_INFO(overlay_depth_only_gpencil_flat_iface, gp_interp_flat)
FLAT(VEC2, aspect)
FLAT(VEC4, sspos)
GPU_SHADER_NAMED_INTERFACE_END(gp_interp_flat)
GPU_SHADER_NAMED_INTERFACE_INFO(overlay_depth_only_gpencil_noperspective_iface,
                                gp_interp_noperspective)
NO_PERSPECTIVE(VEC2, thickness)
NO_PERSPECTIVE(FLOAT, hardness)
GPU_SHADER_NAMED_INTERFACE_END(gp_interp_noperspective)

GPU_SHADER_CREATE_INFO(overlay_depth_gpencil)
DO_STATIC_COMPILATION()
TYPEDEF_SOURCE("gpencil_shader_shared.h")
VERTEX_OUT(overlay_depth_only_gpencil_flat_iface)
VERTEX_OUT(overlay_depth_only_gpencil_noperspective_iface)
VERTEX_SOURCE("overlay_depth_only_gpencil_vert.glsl")
FRAGMENT_SOURCE("overlay_depth_only_gpencil_frag.glsl")
DEPTH_WRITE(DepthWrite::ANY)
PUSH_CONSTANT(BOOL, gpStrokeOrder3d) /* TODO(fclem): Move to a GPencil object UBO. */
PUSH_CONSTANT(VEC4, gpDepthPlane)    /* TODO(fclem): Move to a GPencil object UBO. */
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
ADDITIONAL_INFO(draw_globals)
ADDITIONAL_INFO(draw_gpencil_new)
ADDITIONAL_INFO(draw_object_infos_new)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_depth_pointcloud)
DO_STATIC_COMPILATION()
VERTEX_SOURCE("basic_depth_pointcloud_vert.glsl")
FRAGMENT_SOURCE("overlay_depth_only_frag.glsl")
ADDITIONAL_INFO(draw_pointcloud_new)
ADDITIONAL_INFO(draw_globals)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_depth_curves)
DO_STATIC_COMPILATION()
VERTEX_SOURCE("basic_depth_curves_vert.glsl")
FRAGMENT_SOURCE("overlay_depth_only_frag.glsl")
ADDITIONAL_INFO(draw_hair_new)
ADDITIONAL_INFO(draw_globals)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat_new)
ADDITIONAL_INFO(draw_resource_handle_new)
GPU_SHADER_CREATE_END()

/** \} */

/* -------------------------------------------------------------------- */
/** \name Uniform color
 * \{ */

GPU_SHADER_CREATE_INFO(overlay_uniform_color)
DO_STATIC_COMPILATION()
VERTEX_IN(0, VEC3, pos)
PUSH_CONSTANT(VEC4, ucolor)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_depth_only_vert.glsl")
FRAGMENT_SOURCE("overlay_uniform_color_frag.glsl")
ADDITIONAL_INFO(draw_mesh)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_uniform_color_pointcloud)
DO_STATIC_COMPILATION()
PUSH_CONSTANT(VEC4, ucolor)
FRAGMENT_OUT(0, VEC4, fragColor)
VERTEX_SOURCE("overlay_pointcloud_only_vert.glsl")
FRAGMENT_SOURCE("overlay_uniform_color_frag.glsl")
ADDITIONAL_INFO(draw_pointcloud)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_uniform_color_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_uniform_color)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(overlay_uniform_color_pointcloud_clipped)
DO_STATIC_COMPILATION()
ADDITIONAL_INFO(overlay_uniform_color_pointcloud)
ADDITIONAL_INFO(drw_clipped)
GPU_SHADER_CREATE_END()

/** \} */