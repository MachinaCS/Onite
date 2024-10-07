/* SPDX-FileCopyrightText: 2024 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup gpu
 */

#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(gpu_print)
STORAGE_BUF_FREQ(GPU_SHADER_PRINTF_SLOT, READ_WRITE, uint, gpu_print_buf[], PASS)
DEFINE_VALUE("GPU_SHADER_PRINTF_MAX_CAPACITY", STRINGIFY(GPU_SHADER_PRINTF_MAX_CAPACITY))
DEFINE("GPU_PRINT")
GPU_SHADER_CREATE_END()