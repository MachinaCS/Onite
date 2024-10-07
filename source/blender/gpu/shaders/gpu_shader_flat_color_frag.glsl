/* SPDX-FileCopyrightText: 2016-2022 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_colorspace_lib.glsl"

void main()
{
  fragColor = finalColor;
  fragColor = blender_srgb_to_framebuffer_space(fragColor);
}