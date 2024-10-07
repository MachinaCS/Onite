/* SPDX-FileCopyrightText: 2018 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup bke
 */

#include "BKE_subdiv.hh"

/* NOTE: Was initially used to get proper enumerator types, but this makes
 * it tricky to compile without OpenSubdiv. */
/* #include "opensubdiv_converter_capi.hh" */

struct Mesh;
struct OpenSubdiv_Converter;

namespace blender::bke::subdiv {

struct Settings;

void converter_init_for_mesh(OpenSubdiv_Converter *converter,
                             const Settings *settings,
                             const Mesh *mesh);

/* NOTE: Frees converter data, but not converter itself. This means, that if
 * converter was allocated on heap, it is up to the user to free that memory. */
void converter_free(OpenSubdiv_Converter *converter);

/* ============================ INTERNAL HELPERS ============================ */

/* TODO(sergey): Find a way to make it OpenSubdiv_VtxBoundaryInterpolation,
 * without breaking compilation without OpenSubdiv. */
int converter_vtx_boundary_interpolation_from_settings(const Settings *settings);

/* TODO(sergey): Find a way to make it OpenSubdiv_FVarLinearInterpolation,
 * without breaking compilation without OpenSubdiv. */
int converter_fvar_linear_from_settings(const Settings *settings);

}  // namespace blender::bke::subdiv
