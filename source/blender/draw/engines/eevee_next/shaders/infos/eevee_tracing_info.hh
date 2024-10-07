/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "eevee_defines.hh"
#include "gpu_shader_create_info.hh"

/* -------------------------------------------------------------------- */
/** \name Ray tracing Pipeline
 * \{ */

#define image_out(slot, format, type, name) \
  image(slot, format, Qualifier::WRITE, type, name, Frequency::PASS)
#define image_in(slot, format, type, name) \
  image(slot, format, Qualifier::READ, type, name, Frequency::PASS)

GPU_SHADER_CREATE_INFO(eevee_ray_tile_classify)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_gbuffer_data)
ADDITIONAL_INFO(eevee_global_ubo)
TYPEDEF_SOURCE("draw_shader_shared.hh")
IMAGE_FREQ(0, RAYTRACE_TILEMASK_FORMAT, WRITE, UINT_2D_ARRAY, tile_raytrace_denoise_img, PASS)
IMAGE_FREQ(1, RAYTRACE_TILEMASK_FORMAT, WRITE, UINT_2D_ARRAY, tile_raytrace_tracing_img, PASS)
IMAGE_FREQ(2, RAYTRACE_TILEMASK_FORMAT, WRITE, UINT_2D_ARRAY, tile_horizon_denoise_img, PASS)
IMAGE_FREQ(3, RAYTRACE_TILEMASK_FORMAT, WRITE, UINT_2D_ARRAY, tile_horizon_tracing_img, PASS)
COMPUTE_SOURCE("eevee_ray_tile_classify_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_ray_tile_compact)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_global_ubo)
TYPEDEF_SOURCE("draw_shader_shared.hh")
IMAGE_FREQ(0, RAYTRACE_TILEMASK_FORMAT, READ, UINT_2D_ARRAY, tile_raytrace_denoise_img, PASS)
IMAGE_FREQ(1, RAYTRACE_TILEMASK_FORMAT, READ, UINT_2D_ARRAY, tile_raytrace_tracing_img, PASS)
STORAGE_BUF(0, READ_WRITE, DispatchCommand, raytrace_tracing_dispatch_buf)
STORAGE_BUF(1, READ_WRITE, DispatchCommand, raytrace_denoise_dispatch_buf)
STORAGE_BUF(4, WRITE, uint, raytrace_tracing_tiles_buf[])
STORAGE_BUF(5, WRITE, uint, raytrace_denoise_tiles_buf[])
SPECIALIZATION_CONSTANT(INT, closure_index, 0)
SPECIALIZATION_CONSTANT(INT, resolution_scale, 2)
COMPUTE_SOURCE("eevee_ray_tile_compact_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_ray_generate)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_gbuffer_data)
ADDITIONAL_INFO(eevee_global_ubo)
ADDITIONAL_INFO(eevee_sampling_data)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(eevee_utility_texture)
IMAGE(0, GPU_RGBA16F, WRITE, FLOAT_2D, out_ray_data_img)
STORAGE_BUF(4, READ, uint, tiles_coord_buf[])
SPECIALIZATION_CONSTANT(INT, closure_index, 0)
COMPUTE_SOURCE("eevee_ray_generate_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_ray_trace_fallback)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_gbuffer_data)
ADDITIONAL_INFO(eevee_global_ubo)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(eevee_sampling_data)
ADDITIONAL_INFO(eevee_lightprobe_data)
IMAGE(0, GPU_RGBA16F, READ, FLOAT_2D, ray_data_img)
IMAGE(1, RAYTRACE_RAYTIME_FORMAT, WRITE, FLOAT_2D, ray_time_img)
IMAGE(2, RAYTRACE_RADIANCE_FORMAT, WRITE, FLOAT_2D, ray_radiance_img)
SAMPLER(1, DEPTH_2D, depth_tx)
STORAGE_BUF(5, READ, uint, tiles_coord_buf[])
SPECIALIZATION_CONSTANT(INT, closure_index, 0)
COMPUTE_SOURCE("eevee_ray_trace_fallback_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_ray_trace_planar)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
DEFINE("PLANAR_PROBES")
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_global_ubo)
ADDITIONAL_INFO(eevee_sampling_data)
ADDITIONAL_INFO(eevee_gbuffer_data)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(eevee_lightprobe_data)
ADDITIONAL_INFO(eevee_lightprobe_planar_data)
IMAGE(0, GPU_RGBA16F, READ_WRITE, FLOAT_2D, ray_data_img)
IMAGE(1, RAYTRACE_RAYTIME_FORMAT, WRITE, FLOAT_2D, ray_time_img)
IMAGE(2, RAYTRACE_RADIANCE_FORMAT, WRITE, FLOAT_2D, ray_radiance_img)
SAMPLER(2, DEPTH_2D, depth_tx)
STORAGE_BUF(5, READ, uint, tiles_coord_buf[])
SPECIALIZATION_CONSTANT(INT, closure_index, 0)
COMPUTE_SOURCE("eevee_ray_trace_planar_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_ray_trace_screen)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_global_ubo)
ADDITIONAL_INFO(eevee_sampling_data)
ADDITIONAL_INFO(eevee_gbuffer_data)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(eevee_hiz_data)
ADDITIONAL_INFO(eevee_lightprobe_data)
IMAGE(0, GPU_RGBA16F, READ, FLOAT_2D, ray_data_img)
IMAGE(1, RAYTRACE_RAYTIME_FORMAT, WRITE, FLOAT_2D, ray_time_img)
IMAGE(2, RAYTRACE_RADIANCE_FORMAT, WRITE, FLOAT_2D, ray_radiance_img)
SAMPLER(0, DEPTH_2D, depth_tx)
SAMPLER(1, FLOAT_2D, radiance_front_tx)
SAMPLER(2, FLOAT_2D, radiance_back_tx)
SAMPLER(4, FLOAT_2D, hiz_front_tx)
SAMPLER(5, FLOAT_2D, hiz_back_tx)
STORAGE_BUF(5, READ, uint, tiles_coord_buf[])
COMPUTE_SOURCE("eevee_ray_trace_screen_comp.glsl")
/* Metal: Provide compiler with hint to tune per-thread resource allocation. */
MTL_MAX_TOTAL_THREADS_PER_THREADGROUP(400)
SPECIALIZATION_CONSTANT(BOOL, trace_refraction, true)
SPECIALIZATION_CONSTANT(INT, closure_index, 0)
COMPUTE_SOURCE("eevee_ray_trace_screen_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_ray_denoise_spatial)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_gbuffer_data)
ADDITIONAL_INFO(eevee_global_ubo)
ADDITIONAL_INFO(eevee_sampling_data)
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(eevee_utility_texture)
SAMPLER(3, DEPTH_2D, depth_tx)
IMAGE(0, GPU_RGBA16F, READ, FLOAT_2D, ray_data_img)
IMAGE(1, RAYTRACE_RAYTIME_FORMAT, READ, FLOAT_2D, ray_time_img)
IMAGE(2, RAYTRACE_RADIANCE_FORMAT, READ, FLOAT_2D, ray_radiance_img)
IMAGE(3, RAYTRACE_RADIANCE_FORMAT, WRITE, FLOAT_2D, out_radiance_img)
IMAGE(4, RAYTRACE_VARIANCE_FORMAT, WRITE, FLOAT_2D, out_variance_img)
IMAGE(5, GPU_R32F, WRITE, FLOAT_2D, out_hit_depth_img)
IMAGE(6, RAYTRACE_TILEMASK_FORMAT, READ, UINT_2D_ARRAY, tile_mask_img)
STORAGE_BUF(4, READ, uint, tiles_coord_buf[])
/* Metal: Provide compiler with hint to tune per-thread resource allocation. */
MTL_MAX_TOTAL_THREADS_PER_THREADGROUP(316)
SPECIALIZATION_CONSTANT(INT, raytrace_resolution_scale, 2)
SPECIALIZATION_CONSTANT(BOOL, skip_denoise, false)
SPECIALIZATION_CONSTANT(INT, closure_index, 0)
COMPUTE_SOURCE("eevee_ray_denoise_spatial_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_ray_denoise_temporal)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_global_ubo)
ADDITIONAL_INFO(draw_view)
SAMPLER(0, FLOAT_2D, radiance_history_tx)
SAMPLER(1, FLOAT_2D, variance_history_tx)
SAMPLER(2, UINT_2D_ARRAY, tilemask_history_tx)
SAMPLER(3, DEPTH_2D, depth_tx)
IMAGE(0, GPU_R32F, READ, FLOAT_2D, hit_depth_img)
IMAGE(1, RAYTRACE_RADIANCE_FORMAT, READ, FLOAT_2D, in_radiance_img)
IMAGE(2, RAYTRACE_RADIANCE_FORMAT, WRITE, FLOAT_2D, out_radiance_img)
IMAGE(3, RAYTRACE_VARIANCE_FORMAT, READ, FLOAT_2D, in_variance_img)
IMAGE(4, RAYTRACE_VARIANCE_FORMAT, WRITE, FLOAT_2D, out_variance_img)
STORAGE_BUF(4, READ, uint, tiles_coord_buf[])
COMPUTE_SOURCE("eevee_ray_denoise_temporal_comp.glsl")
/* Metal: Provide compiler with hint to tune per-thread resource allocation. */
MTL_MAX_TOTAL_THREADS_PER_THREADGROUP(512)
SPECIALIZATION_CONSTANT(INT, closure_index, 0)
COMPUTE_SOURCE("eevee_ray_denoise_temporal_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_ray_denoise_bilateral)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_gbuffer_data)
ADDITIONAL_INFO(eevee_global_ubo)
ADDITIONAL_INFO(eevee_sampling_data)
ADDITIONAL_INFO(draw_view)
SAMPLER(1, DEPTH_2D, depth_tx)
IMAGE(1, RAYTRACE_RADIANCE_FORMAT, READ, FLOAT_2D, in_radiance_img)
IMAGE(2, RAYTRACE_RADIANCE_FORMAT, WRITE, FLOAT_2D, out_radiance_img)
IMAGE(3, RAYTRACE_VARIANCE_FORMAT, READ, FLOAT_2D, in_variance_img)
IMAGE(6, RAYTRACE_TILEMASK_FORMAT, READ, UINT_2D_ARRAY, tile_mask_img)
STORAGE_BUF(4, READ, uint, tiles_coord_buf[])
SPECIALIZATION_CONSTANT(INT, closure_index, 0)
COMPUTE_SOURCE("eevee_ray_denoise_bilateral_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_horizon_setup)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_gbuffer_data)
ADDITIONAL_INFO(eevee_global_ubo)
ADDITIONAL_INFO(draw_view)
SAMPLER(0, DEPTH_2D, depth_tx)
SAMPLER(1, FLOAT_2D, in_radiance_tx)
IMAGE(2, RAYTRACE_RADIANCE_FORMAT, WRITE, FLOAT_2D, out_radiance_img)
IMAGE(3, GPU_RGB10_A2, WRITE, FLOAT_2D, out_normal_img)
COMPUTE_SOURCE("eevee_horizon_setup_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_horizon_scan)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_gbuffer_data)
ADDITIONAL_INFO(eevee_global_ubo)
ADDITIONAL_INFO(eevee_sampling_data)
ADDITIONAL_INFO(eevee_utility_texture)
ADDITIONAL_INFO(eevee_hiz_data)
ADDITIONAL_INFO(draw_view)
SPECIALIZATION_CONSTANT(INT, fast_gi_slice_count, 2)
SPECIALIZATION_CONSTANT(INT, fast_gi_step_count, 8)
SPECIALIZATION_CONSTANT(BOOL, fast_gi_ao_only, false)
SAMPLER(0, FLOAT_2D, screen_radiance_tx)
SAMPLER(1, FLOAT_2D, screen_normal_tx)
IMAGE(2, GPU_RGBA16F, WRITE, FLOAT_2D, horizon_radiance_0_img)
IMAGE(3, GPU_RGBA8, WRITE, FLOAT_2D, horizon_radiance_1_img)
IMAGE(4, GPU_RGBA8, WRITE, FLOAT_2D, horizon_radiance_2_img)
IMAGE(5, GPU_RGBA8, WRITE, FLOAT_2D, horizon_radiance_3_img)
STORAGE_BUF(7, READ, uint, tiles_coord_buf[])
/* Metal: Provide compiler with hint to tune per-thread resource allocation. */
MTL_MAX_TOTAL_THREADS_PER_THREADGROUP(400)
COMPUTE_SOURCE("eevee_horizon_scan_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_horizon_denoise)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_global_ubo)
ADDITIONAL_INFO(eevee_sampling_data)
ADDITIONAL_INFO(eevee_hiz_data)
ADDITIONAL_INFO(draw_view)
SAMPLER(2, FLOAT_2D, in_sh_0_tx)
SAMPLER(4, FLOAT_2D, in_sh_1_tx)
SAMPLER(5, FLOAT_2D, in_sh_2_tx)
SAMPLER(6, FLOAT_2D, in_sh_3_tx)
SAMPLER(7, FLOAT_2D, screen_normal_tx)
IMAGE(2, GPU_RGBA16F, WRITE, FLOAT_2D, out_sh_0_img)
IMAGE(3, GPU_RGBA8, WRITE, FLOAT_2D, out_sh_1_img)
IMAGE(4, GPU_RGBA8, WRITE, FLOAT_2D, out_sh_2_img)
IMAGE(5, GPU_RGBA8, WRITE, FLOAT_2D, out_sh_3_img)
STORAGE_BUF(7, READ, uint, tiles_coord_buf[])
COMPUTE_SOURCE("eevee_horizon_denoise_comp.glsl")
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(eevee_horizon_resolve)
DO_STATIC_COMPILATION()
LOCAL_GROUP_SIZE(RAYTRACE_GROUP_SIZE, RAYTRACE_GROUP_SIZE)
ADDITIONAL_INFO(eevee_shared)
ADDITIONAL_INFO(eevee_gbuffer_data)
ADDITIONAL_INFO(eevee_global_ubo)
ADDITIONAL_INFO(eevee_sampling_data)
ADDITIONAL_INFO(eevee_lightprobe_data)
ADDITIONAL_INFO(draw_view)
SAMPLER(1, DEPTH_2D, depth_tx)
SAMPLER(2, FLOAT_2D, horizon_radiance_0_tx)
SAMPLER(3, FLOAT_2D, horizon_radiance_1_tx)
SAMPLER(4, FLOAT_2D, horizon_radiance_2_tx)
SAMPLER(5, FLOAT_2D, horizon_radiance_3_tx)
SAMPLER(8, FLOAT_2D, screen_normal_tx)
IMAGE(3, RAYTRACE_RADIANCE_FORMAT, READ_WRITE, FLOAT_2D, closure0_img)
IMAGE(4, RAYTRACE_RADIANCE_FORMAT, READ_WRITE, FLOAT_2D, closure1_img)
IMAGE(5, RAYTRACE_RADIANCE_FORMAT, READ_WRITE, FLOAT_2D, closure2_img)
STORAGE_BUF(7, READ, uint, tiles_coord_buf[])
/* Metal: Provide compiler with hint to tune per-thread resource allocation. */
MTL_MAX_TOTAL_THREADS_PER_THREADGROUP(400)
COMPUTE_SOURCE("eevee_horizon_resolve_comp.glsl")
GPU_SHADER_CREATE_END()

#undef image_out
#undef image_in

/** \} */