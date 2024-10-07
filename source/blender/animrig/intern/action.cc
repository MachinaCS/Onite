/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup animrig
 */

#include "DNA_action_defaults.h"
#include "DNA_action_types.h"
#include "DNA_anim_types.h"
#include "DNA_array_utils.hh"
#include "DNA_defaults.h"
#include "DNA_scene_types.h"

#include "BLI_listbase.h"
#include "BLI_listbase_wrapper.hh"
#include "BLI_map.hh"
#include "BLI_math_base.h"
#include "BLI_string.h"
#include "BLI_string_utf8.h"
#include "BLI_string_utils.hh"
#include "BLI_utildefines.h"

#include "BKE_action.hh"
#include "BKE_anim_data.hh"
#include "BKE_fcurve.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_nla.hh"
#include "BKE_preview_image.hh"
#include "BKE_report.hh"

#include "RNA_access.hh"
#include "RNA_path.hh"
#include "RNA_prototypes.hh"

#include "ED_keyframing.hh"

#include "MEM_guardedalloc.h"

#include "BLT_translation.hh"

#include "DEG_depsgraph_build.hh"

#include "ANIM_action.hh"
#include "ANIM_action_iterators.hh"
#include "ANIM_action_legacy.hh"
#include "ANIM_animdata.hh"
#include "ANIM_fcurve.hh"
#include "ANIM_nla.hh"

#include "action_runtime.hh"

#include "atomic_ops.h"

#include <cstdio>
#include <cstring>

namespace blender::animrig {

namespace {
/**
 * Default name for action slots. The first two characters in the name indicate the ID type
 * of whatever is animated by it.
 *
 * Since the ID type may not be determined when the slot is created, the prefix starts out at
 * XX. Note that no code should use this XX value; use Slot::has_idtype() instead.
 */
constexpr const char *slot_default_name = "Slot";
constexpr const char *slot_unbound_prefix = "XX";

constexpr const char *layer_default_name = "Layer";

}  // namespace

static animrig::Layer &ActionLayer_alloc()
{
  ActionLayer *layer = DNA_struct_default_alloc(ActionLayer);
  return layer->wrap();
}

/* Copied from source/blender/blenkernel/intern/grease_pencil.cc.
 * Keep an eye on DNA_array_utils.hh; we may want to move these functions in there. */
template<typename T> static void grow_array(T **array, int *num, const int add_num)
{
  BLI_assert(add_num > 0);
  const int new_array_num = *num + add_num;
  T *new_array = MEM_cnew_array<T>(new_array_num, "animrig::action/grow_array");

  blender::uninitialized_relocate_n(*array, *num, new_array);
  MEM_SAFE_FREE(*array);

  *array = new_array;
  *num = new_array_num;
}

template<typename T> static void grow_array_and_append(T **array, int *num, T item)
{
  grow_array(array, num, 1);
  (*array)[*num - 1] = item;
}

template<typename T>
static void grow_array_and_insert(T **array, int *num, const int index, T item)
{
  BLI_assert(index >= 0 && index <= *num);
  const int new_array_num = *num + 1;
  T *new_array = MEM_cnew_array<T>(new_array_num, __func__);

  blender::uninitialized_relocate_n(*array, index, new_array);
  new_array[index] = item;
  blender::uninitialized_relocate_n(*array + index, *num - index, new_array + index + 1);

  MEM_SAFE_FREE(*array);

  *array = new_array;
  *num = new_array_num;
}

template<typename T> static void shrink_array(T **array, int *num, const int shrink_num)
{
  BLI_assert(shrink_num > 0);
  const int new_array_num = *num - shrink_num;
  T *new_array = MEM_cnew_array<T>(new_array_num, __func__);

  blender::uninitialized_move_n(*array, new_array_num, new_array);
  MEM_freeN(*array);

  *array = new_array;
  *num = new_array_num;
}

template<typename T> static void shrink_array_and_remove(T **array, int *num, const int index)
{
  BLI_assert(index >= 0 && index < *num);
  const int new_array_num = *num - 1;
  T *new_array = MEM_cnew_array<T>(new_array_num, __func__);

  blender::uninitialized_move_n(*array, index, new_array);
  blender::uninitialized_move_n(*array + index + 1, *num - index - 1, new_array + index);
  MEM_freeN(*array);

  *array = new_array;
  *num = new_array_num;
}

/**
 * Same as `shrink_array_and_remove()` above, except instead of shifting all the
 * elements after the removed item over to fill the gap, it just swaps in the last
 * element to where the removed element was.
 */
template<typename T> static void shrink_array_and_swap_remove(T **array, int *num, const int index)
{
  BLI_assert(index >= 0 && index < *num);
  const int new_array_num = *num - 1;
  T *new_array = MEM_cnew_array<T>(new_array_num, __func__);

  blender::uninitialized_move_n(*array, index, new_array);
  if (index < new_array_num) {
    new_array[index] = (*array)[new_array_num];
    blender::uninitialized_move_n(*array + index + 1, *num - index - 2, new_array + index + 1);
  }
  MEM_freeN(*array);

  *array = new_array;
  *num = new_array_num;
}

/**
 * Moves the given (end exclusive) range to index `to`, shifting other items
 * before/after to make room.
 *
 * The range is moved such that the *start* ends up at `to`.
 *
 * `to` *must* be far away enough from the end of the array for the entire range
 * to be moved there without spilling over the end of the array.
 */
template<typename T>
static void array_shift_range(
    T *array, const int num, const int range_start, const int range_end, const int to)
{
  BLI_assert(range_start <= range_end);
  BLI_assert(range_end <= num);
  BLI_assert(to <= num + range_start - range_end);
  UNUSED_VARS_NDEBUG(num);

  if (ELEM(range_start, range_end, to)) {
    return;
  }

  if (to < range_start) {
    T *start = array + to;
    T *mid = array + range_start;
    T *end = array + range_end;
    std::rotate(start, mid, end);
  }
  else {
    T *start = array + range_start;
    T *mid = array + range_end;
    T *end = array + to + range_end - range_start;
    std::rotate(start, mid, end);
  }
}

/* ----- Action implementation ----------- */

bool Action::is_empty() const
{
  /* The check for emptiness has to include the check for an empty `groups` ListBase because of the
   * animation filtering code. With the functions `rearrange_action_channels` and
   * `join_groups_action_temp` the ownership of FCurves is temporarily transferred to the `groups`
   * ListBase leaving `curves` potentially empty. */
  return this->layer_array_num == 0 && this->slot_array_num == 0 &&
         BLI_listbase_is_empty(&this->curves) && BLI_listbase_is_empty(&this->groups);
}
bool Action::is_action_legacy() const
{
  /* This is a valid legacy Action only if there is no layered info. */
  return this->layer_array_num == 0 && this->slot_array_num == 0;
}
bool Action::is_action_layered() const
{
  /* This is a valid layered Action if there is ANY layered info (because that
   * takes precedence) or when there is no legacy info. */
  return this->layer_array_num > 0 || this->slot_array_num > 0 ||
         (BLI_listbase_is_empty(&this->curves) && BLI_listbase_is_empty(&this->groups));
}

blender::Span<const Layer *> Action::layers() const
{
  return blender::Span<const Layer *>{reinterpret_cast<Layer **>(this->layer_array),
                                      this->layer_array_num};
}
blender::Span<Layer *> Action::layers()
{
  return blender::Span<Layer *>{reinterpret_cast<Layer **>(this->layer_array),
                                this->layer_array_num};
}
const Layer *Action::layer(const int64_t index) const
{
  return &this->layer_array[index]->wrap();
}
Layer *Action::layer(const int64_t index)
{
  return &this->layer_array[index]->wrap();
}

Layer &Action::layer_add(const std::optional<StringRefNull> name)
{
  Layer &new_layer = ActionLayer_alloc();
  if (name.has_value()) {
    STRNCPY_UTF8(new_layer.name, name.value().c_str());
  }
  else {
    STRNCPY_UTF8(new_layer.name, layer_default_name);
  }

  grow_array_and_append<::ActionLayer *>(&this->layer_array, &this->layer_array_num, &new_layer);
  this->layer_active_index = this->layer_array_num - 1;

  /* If this is the first layer in this Action, it means that it could have been
   * used as a legacy Action before. As a result, this->idroot may be non-zero
   * while it should be zero for layered Actions.
   *
   * And since setting this to 0 when it is already supposed to be 0 is fine,
   * there is no check for whether this is actually the first layer. */
  this->idroot = 0;

  return new_layer;
}

static void layer_ptr_destructor(ActionLayer **dna_layer_ptr)
{
  Layer &layer = (*dna_layer_ptr)->wrap();
  MEM_delete(&layer);
};

bool Action::layer_remove(Layer &layer_to_remove)
{
  const int64_t layer_index = this->find_layer_index(layer_to_remove);
  if (layer_index < 0) {
    return false;
  }

  dna::array::remove_index(&this->layer_array,
                           &this->layer_array_num,
                           &this->layer_active_index,
                           layer_index,
                           layer_ptr_destructor);
  return true;
}

void Action::layer_keystrip_ensure()
{
  /* Ensure a layer. */
  Layer *layer;
  if (this->layers().is_empty()) {
    layer = &this->layer_add(DATA_(layer_default_name));
  }
  else {
    layer = this->layer(0);
  }

  /* Ensure a keyframe Strip. */
  if (layer->strips().is_empty()) {
    layer->strip_add(*this, Strip::Type::Keyframe);
  }

  /* Within the limits of Baklava Phase 1, the above code should not have
   * created more than one layer, or more than one strip on the layer. And if a
   * layer + strip already existed, that must have been a keyframe strip. */
  assert_baklava_phase_1_invariants(*this);
}

int64_t Action::find_layer_index(const Layer &layer) const
{
  for (const int64_t layer_index : this->layers().index_range()) {
    const Layer *visit_layer = this->layer(layer_index);
    if (visit_layer == &layer) {
      return layer_index;
    }
  }
  return -1;
}

int64_t Action::find_slot_index(const Slot &slot) const
{
  for (const int64_t slot_index : this->slots().index_range()) {
    const Slot *visit_slot = this->slot(slot_index);
    if (visit_slot == &slot) {
      return slot_index;
    }
  }
  return -1;
}

blender::Span<const Slot *> Action::slots() const
{
  return blender::Span<Slot *>{reinterpret_cast<Slot **>(this->slot_array), this->slot_array_num};
}
blender::Span<Slot *> Action::slots()
{
  return blender::Span<Slot *>{reinterpret_cast<Slot **>(this->slot_array), this->slot_array_num};
}
const Slot *Action::slot(const int64_t index) const
{
  return &this->slot_array[index]->wrap();
}
Slot *Action::slot(const int64_t index)
{
  return &this->slot_array[index]->wrap();
}

Slot *Action::slot_for_handle(const slot_handle_t handle)
{
  const Slot *slot = const_cast<const Action *>(this)->slot_for_handle(handle);
  return const_cast<Slot *>(slot);
}

const Slot *Action::slot_for_handle(const slot_handle_t handle) const
{
  if (handle == Slot::unassigned) {
    return nullptr;
  }

  /* TODO: implement hash-map lookup. */
  for (const Slot *slot : slots()) {
    if (slot->handle == handle) {
      return slot;
    }
  }
  return nullptr;
}

static void slot_name_ensure_unique(Action &action, Slot &slot)
{
  /* Cannot capture parameters by reference in the lambda, as that would change its signature
   * and no longer be compatible with BLI_uniquename_cb(). That's why this struct is necessary. */
  struct DupNameCheckData {
    Action &action;
    Slot &slot;
  };
  DupNameCheckData check_data = {action, slot};

  auto check_name_is_used = [](void *arg, const char *name) -> bool {
    DupNameCheckData *data = static_cast<DupNameCheckData *>(arg);
    for (const Slot *slot : data->action.slots()) {
      if (slot == &data->slot) {
        /* Don't compare against the slot that's being renamed. */
        continue;
      }
      if (STREQ(slot->name, name)) {
        return true;
      }
    }
    return false;
  };

  BLI_uniquename_cb(check_name_is_used, &check_data, "", '.', slot.name, sizeof(slot.name));
}

/* TODO: maybe this function should only set the 'name without prefix' aka the 'display name'. That
 * way only `this->id_type` is responsible for the prefix. I (Sybren) think that's easier to
 * determine when the code is a bit more mature, and we can see what the majority of the calls to
 * this function actually do/need. */
void Action::slot_name_set(Main &bmain, Slot &slot, const StringRefNull new_name)
{
  this->slot_name_define(slot, new_name);
  this->slot_name_propagate(bmain, slot);
}

void Action::slot_name_define(Slot &slot, const StringRefNull new_name)
{
  BLI_assert_msg(StringRef(new_name).size() >= Slot::name_length_min,
                 "Action Slots must be large enough for a 2-letter ID code + the display name");
  STRNCPY_UTF8(slot.name, new_name.c_str());
  slot_name_ensure_unique(*this, slot);
}

void Action::slot_name_propagate(Main &bmain, const Slot &slot)
{
  /* Just loop over all animatable IDs in the main database. */
  ListBase *lb;
  ID *id;
  FOREACH_MAIN_LISTBASE_BEGIN (&bmain, lb) {
    FOREACH_MAIN_LISTBASE_ID_BEGIN (lb, id) {
      if (!id_can_have_animdata(id)) {
        /* This ID type cannot have any animation, so ignore all and continue to
         * the next ID type. */
        break;
      }

      AnimData *adt = BKE_animdata_from_id(id);
      if (!adt || adt->action != this) {
        /* Not animated by this Action. */
        continue;
      }
      if (adt->slot_handle != slot.handle) {
        /* Not animated by this Slot. */
        continue;
      }

      /* Ensure the Slot name on the AnimData is correct. */
      STRNCPY_UTF8(adt->slot_name, slot.name);
    }
    FOREACH_MAIN_LISTBASE_ID_END;
  }
  FOREACH_MAIN_LISTBASE_END;
}

Slot *Action::slot_find_by_name(const StringRefNull slot_name)
{
  for (Slot *slot : slots()) {
    if (STREQ(slot->name, slot_name.c_str())) {
      return slot;
    }
  }
  return nullptr;
}

Slot &Action::slot_allocate()
{
  Slot &slot = *MEM_new<Slot>(__func__);
  this->last_slot_handle++;
  BLI_assert_msg(this->last_slot_handle > 0, "Action Slot handle overflow");
  slot.handle = this->last_slot_handle;

  /* Set the default flags. These cannot be set via the 'DNA defaults' system,
   * as that would require knowing which bit corresponds with which flag. That's
   * only known to the C++ wrapper code. */
  slot.set_expanded(true);
  return slot;
}

Slot &Action::slot_add()
{
  Slot &slot = this->slot_allocate();

  /* Assign the default name and the 'unbound' name prefix. */
  STRNCPY_UTF8(slot.name, slot_unbound_prefix);
  BLI_strncpy_utf8(slot.name + 2, DATA_(slot_default_name), ARRAY_SIZE(slot.name) - 2);

  /* Append the Slot to the Action. */
  grow_array_and_append<::ActionSlot *>(&this->slot_array, &this->slot_array_num, &slot);

  slot_name_ensure_unique(*this, slot);

  /* If this is the first slot in this Action, it means that it could have
   * been used as a legacy Action before. As a result, this->idroot may be
   * non-zero while it should be zero for layered Actions.
   *
   * And since setting this to 0 when it is already supposed to be 0 is fine,
   * there is no check for whether this is actually the first layer. */
  this->idroot = 0;

  return slot;
}

Slot &Action::slot_add_for_id(const ID &animated_id)
{
  Slot &slot = this->slot_add();

  slot.idtype = GS(animated_id.name);
  this->slot_name_define(slot, animated_id.name);

  /* No need to call anim.slot_name_propagate() as nothing will be using
   * this brand new Slot yet. */

  return slot;
}

static void slot_ptr_destructor(ActionSlot **dna_slot_ptr)
{
  Slot &slot = (*dna_slot_ptr)->wrap();
  MEM_delete(&slot);
};

bool Action::slot_remove(Slot &slot_to_remove)
{
  /* Check that this slot belongs to this Action. */
  const int64_t slot_index = this->find_slot_index(slot_to_remove);
  if (slot_index < 0) {
    return false;
  }

  /* Remove the slot's data from each layer. */
  for (Layer *layer : this->layers()) {
    layer->slot_data_remove(*this, slot_to_remove.handle);
  }

  /* Don't bother un-assigning this slot from its users. The slot handle will
   * not be reused by a new slot anyway. */

  /* Remove the actual slot. */
  dna::array::remove_index(
      &this->slot_array, &this->slot_array_num, nullptr, slot_index, slot_ptr_destructor);
  return true;
}

void Action::slot_active_set(const slot_handle_t slot_handle)
{
  for (Slot *slot : slots()) {
    slot->set_active(slot->handle == slot_handle);
  }
}

Slot *Action::slot_active_get()
{
  for (Slot *slot : slots()) {
    if (slot->is_active()) {
      return slot;
    }
  }
  return nullptr;
}

Slot *Action::find_suitable_slot_for(const ID &animated_id)
{
  AnimData *adt = BKE_animdata_from_id(&animated_id);

  /* The slot handle is only valid when this action has already been
   * assigned. Otherwise it's meaningless. */
  if (adt && adt->action == this) {
    Slot *slot = this->slot_for_handle(adt->slot_handle);
    if (slot && slot->is_suitable_for(animated_id)) {
      return slot;
    }
  }

  /* Try the slot name from the AnimData, if it is set. */
  if (adt && adt->slot_name[0]) {
    Slot *slot = this->slot_find_by_name(adt->slot_name);
    if (slot && slot->is_suitable_for(animated_id)) {
      return slot;
    }
  }

  /* As a last resort, search for the ID name. */
  Slot *slot = this->slot_find_by_name(animated_id.name);
  if (slot && slot->is_suitable_for(animated_id)) {
    return slot;
  }

  return nullptr;
}

bool Action::is_slot_animated(const slot_handle_t slot_handle) const
{
  if (slot_handle == Slot::unassigned) {
    return false;
  }

  Span<const FCurve *> fcurves = fcurves_for_action_slot(*this, slot_handle);
  return !fcurves.is_empty();
}

int Action::strip_keyframe_data_append(StripKeyframeData *strip_data)
{
  BLI_assert(strip_data != nullptr);

  grow_array_and_append<ActionStripKeyframeData *>(
      &this->strip_keyframe_data_array, &this->strip_keyframe_data_array_num, strip_data);

  return this->strip_keyframe_data_array_num - 1;
}

void Action::strip_keyframe_data_remove_if_unused(const int index)
{
  BLI_assert(index >= 0 && index < this->strip_keyframe_data_array_num);

  /* Make sure the data isn't being used anywhere. */
  for (const Layer *layer : this->layers()) {
    for (const Strip *strip : layer->strips()) {
      if (strip->type() == Strip::Type::Keyframe && strip->data_index == index) {
        return;
      }
    }
  }

  /* Free the item to be removed. */
  MEM_delete<StripKeyframeData>(
      static_cast<StripKeyframeData *>(this->strip_keyframe_data_array[index]));

  /* Remove the item, swapping in the item at the end of the array. */
  shrink_array_and_swap_remove<ActionStripKeyframeData *>(
      &this->strip_keyframe_data_array, &this->strip_keyframe_data_array_num, index);

  /* Update strips that pointed at the swapped-in item.
   *
   * Note that we don't special-case the corner-case where the removed data was
   * at the end of the array, but it ends up not mattering because then
   * `old_index == index`. */
  const int old_index = this->strip_keyframe_data_array_num;
  for (Layer *layer : this->layers()) {
    for (Strip *strip : layer->strips()) {
      if (strip->type() == Strip::Type::Keyframe && strip->data_index == old_index) {
        strip->data_index = index;
      }
    }
  }
}

Span<const StripKeyframeData *> Action::strip_keyframe_data() const
{
  /* The reinterpret cast is needed because `strip_keyframe_data_array` is for
   * pointers to the C type `ActionStripKeyframeData`, but we want the C++
   * wrapper type `StripKeyframeData`. */
  return Span<StripKeyframeData *>{
      reinterpret_cast<StripKeyframeData **>(this->strip_keyframe_data_array),
      this->strip_keyframe_data_array_num};
}
Span<StripKeyframeData *> Action::strip_keyframe_data()
{
  /* The reinterpret cast is needed because `strip_keyframe_data_array` is for
   * pointers to the C type `ActionStripKeyframeData`, but we want the C++
   * wrapper type `StripKeyframeData`. */
  return Span<StripKeyframeData *>{
      reinterpret_cast<StripKeyframeData **>(this->strip_keyframe_data_array),
      this->strip_keyframe_data_array_num};
}

Layer *Action::get_layer_for_keyframing()
{
  assert_baklava_phase_1_invariants(*this);

  if (this->layers().is_empty()) {
    return nullptr;
  }

  return this->layer(0);
}

void Action::slot_name_ensure_prefix(Slot &slot)
{
  slot.name_ensure_prefix();
  slot_name_ensure_unique(*this, slot);
}

void Action::slot_setup_for_id(Slot &slot, const ID &animated_id)
{
  if (slot.has_idtype()) {
    BLI_assert(slot.idtype == GS(animated_id.name));
    return;
  }

  slot.idtype = GS(animated_id.name);
  this->slot_name_ensure_prefix(slot);
}

bool Action::has_keyframes(const slot_handle_t action_slot_handle) const
{
  if (this->is_action_legacy()) {
    /* Old BKE_action_has_motion(const bAction *act) implementation. */
    LISTBASE_FOREACH (const FCurve *, fcu, &this->curves) {
      if (fcu->totvert) {
        return true;
      }
    }
    return false;
  }

  for (const FCurve *fcu : fcurves_for_action_slot(*this, action_slot_handle)) {
    if (fcu->totvert) {
      return true;
    }
  }
  return false;
}

bool Action::has_single_frame() const
{
  bool found_key = false;
  float found_key_frame = 0.0f;

  for (const FCurve *fcu : legacy::fcurves_all(this)) {
    switch (fcu->totvert) {
      case 0:
        /* No keys, so impossible to come to a conclusion on this curve alone. */
        continue;
      case 1:
        /* Single key, which is the complex case, so handle below. */
        break;
      default:
        /* Multiple keys, so there is animation. */
        return false;
    }

    const float this_key_frame = fcu->bezt != nullptr ? fcu->bezt[0].vec[1][0] :
                                                        fcu->fpt[0].vec[0];
    if (!found_key) {
      found_key = true;
      found_key_frame = this_key_frame;
      continue;
    }

    /* The graph editor rounds to 1/1000th of a frame, so it's not necessary to be really precise
     * with these comparisons. */
    if (!compare_ff(found_key_frame, this_key_frame, 0.001f)) {
      /* This key differs from the already-found key, so this Action represents animation. */
      return false;
    }
  }

  /* There is only a single frame if we found at least one key. */
  return found_key;
}

bool Action::is_cyclic() const
{
  return (this->flag & ACT_FRAME_RANGE) && (this->flag & ACT_CYCLIC);
}

/** Return the frame range of the span of keys. */
static float2 get_frame_range_of_fcurves(Span<const FCurve *> fcurves, bool include_modifiers);

float2 Action::get_frame_range() const
{
  if (this->flag & ACT_FRAME_RANGE) {
    return {this->frame_start, this->frame_end};
  }

  Vector<const FCurve *> all_fcurves = legacy::fcurves_all(this);
  return get_frame_range_of_fcurves(all_fcurves, false);
}

float2 Action::get_frame_range_of_slot(const slot_handle_t slot_handle) const
{
  if (this->flag & ACT_FRAME_RANGE) {
    return {this->frame_start, this->frame_end};
  }

  Vector<const FCurve *> legacy_fcurves;
  Span<const FCurve *> fcurves_to_consider;

  if (this->is_action_layered()) {
    fcurves_to_consider = fcurves_for_action_slot(*this, slot_handle);
  }
  else {
    legacy_fcurves = legacy::fcurves_all(this);
    fcurves_to_consider = legacy_fcurves;
  }

  return get_frame_range_of_fcurves(fcurves_to_consider, false);
}

float2 Action::get_frame_range_of_keys(const bool include_modifiers) const
{
  return get_frame_range_of_fcurves(legacy::fcurves_all(this), include_modifiers);
}

static float2 get_frame_range_of_fcurves(Span<const FCurve *> fcurves,
                                         const bool include_modifiers)
{
  float min = 999999999.0f, max = -999999999.0f;
  bool foundvert = false, foundmod = false;

  for (const FCurve *fcu : fcurves) {
    /* if curve has keyframes, consider them first */
    if (fcu->totvert) {
      float nmin, nmax;

      /* get extents for this curve
       * - no "selected only", since this is often used in the backend
       * - no "minimum length" (we will apply this later), otherwise
       *   single-keyframe curves will increase the overall length by
       *   a phantom frame (#50354)
       */
      BKE_fcurve_calc_range(fcu, &nmin, &nmax, false);

      /* compare to the running tally */
      min = min_ff(min, nmin);
      max = max_ff(max, nmax);

      foundvert = true;
    }

    /* if include_modifiers is enabled, need to consider modifiers too
     * - only really care about the last modifier
     */
    if ((include_modifiers) && (fcu->modifiers.last)) {
      FModifier *fcm = static_cast<FModifier *>(fcu->modifiers.last);

      /* only use the maximum sensible limits of the modifiers if they are more extreme */
      switch (fcm->type) {
        case FMODIFIER_TYPE_LIMITS: /* Limits F-Modifier */
        {
          FMod_Limits *fmd = (FMod_Limits *)fcm->data;

          if (fmd->flag & FCM_LIMIT_XMIN) {
            min = min_ff(min, fmd->rect.xmin);
          }
          if (fmd->flag & FCM_LIMIT_XMAX) {
            max = max_ff(max, fmd->rect.xmax);
          }
          break;
        }
        case FMODIFIER_TYPE_CYCLES: /* Cycles F-Modifier */
        {
          FMod_Cycles *fmd = (FMod_Cycles *)fcm->data;

          if (fmd->before_mode != FCM_EXTRAPOLATE_NONE) {
            min = MINAFRAMEF;
          }
          if (fmd->after_mode != FCM_EXTRAPOLATE_NONE) {
            max = MAXFRAMEF;
          }
          break;
        }
          /* TODO: function modifier may need some special limits */

        default: /* all other standard modifiers are on the infinite range... */
          min = MINAFRAMEF;
          max = MAXFRAMEF;
          break;
      }

      foundmod = true;
    }

    /* This block is here just so that editors/IDEs do not get confused about the two opening
     * curly braces in the `#ifdef WITH_ANIM_BAKLAVA` block above, but one closing curly brace
     * here. */
  }

  if (foundvert || foundmod) {
    return float2{max_ff(min, MINAFRAMEF), min_ff(max, MAXFRAMEF)};
  }

  return float2{0.0f, 0.0f};
}

/* ----- ActionLayer implementation ----------- */

Layer *Layer::duplicate_with_shallow_strip_copies(const StringRefNull allocation_name) const
{
  ActionLayer *copy = MEM_cnew<ActionLayer>(allocation_name.c_str());
  *copy = *reinterpret_cast<const ActionLayer *>(this);

  /* Make a shallow copy of the Strips, without copying their data. */
  copy->strip_array = MEM_cnew_array<ActionStrip *>(this->strip_array_num,
                                                    allocation_name.c_str());
  for (int i : this->strips().index_range()) {
    Strip *strip_copy = MEM_new<Strip>(allocation_name.c_str(), *this->strip(i));
    copy->strip_array[i] = strip_copy;
  }

  return &copy->wrap();
}

Layer::~Layer()
{
  for (Strip *strip : this->strips()) {
    MEM_delete(strip);
  }
  MEM_SAFE_FREE(this->strip_array);
  this->strip_array_num = 0;
}

blender::Span<const Strip *> Layer::strips() const
{
  return blender::Span<Strip *>{reinterpret_cast<Strip **>(this->strip_array),
                                this->strip_array_num};
}
blender::Span<Strip *> Layer::strips()
{
  return blender::Span<Strip *>{reinterpret_cast<Strip **>(this->strip_array),
                                this->strip_array_num};
}
const Strip *Layer::strip(const int64_t index) const
{
  return &this->strip_array[index]->wrap();
}
Strip *Layer::strip(const int64_t index)
{
  return &this->strip_array[index]->wrap();
}

Strip &Layer::strip_add(Action &owning_action, const Strip::Type strip_type)
{
  Strip &strip = Strip::create(owning_action, strip_type);

  /* Add the new strip to the strip array. */
  grow_array_and_append<::ActionStrip *>(&this->strip_array, &this->strip_array_num, &strip);

  return strip;
}

static void strip_ptr_destructor(ActionStrip **dna_strip_ptr)
{
  Strip &strip = (*dna_strip_ptr)->wrap();
  MEM_delete(&strip);
};

bool Layer::strip_remove(Action &owning_action, Strip &strip)
{
  const int64_t strip_index = this->find_strip_index(strip);
  if (strip_index < 0) {
    return false;
  }

  const Strip::Type strip_type = strip.type();
  const int data_index = strip.data_index;

  dna::array::remove_index(
      &this->strip_array, &this->strip_array_num, nullptr, strip_index, strip_ptr_destructor);

  /* It's important that we do this *after* removing the strip itself
   * (immediately above), because otherwise the strip will be found as a
   * still-existing user of the strip data and thus the strip data won't be
   * removed even if this strip was the last user. */
  switch (strip_type) {
    case Strip::Type::Keyframe:
      owning_action.strip_keyframe_data_remove_if_unused(data_index);
      break;
  }

  return true;
}

int64_t Layer::find_strip_index(const Strip &strip) const
{
  for (const int64_t strip_index : this->strips().index_range()) {
    const Strip *visit_strip = this->strip(strip_index);
    if (visit_strip == &strip) {
      return strip_index;
    }
  }
  return -1;
}

void Layer::slot_data_remove(Action &owning_action, const slot_handle_t slot_handle)
{
  for (Strip *strip : this->strips()) {
    strip->slot_data_remove(owning_action, slot_handle);
  }
}

/* ----- ActionSlot implementation ----------- */

Slot::Slot()
{
  memset(this, 0, sizeof(*this));
  this->runtime = MEM_new<SlotRuntime>(__func__);
}

Slot::Slot(const Slot &other)
{
  memcpy(this, &other, sizeof(*this));
  this->runtime = MEM_new<SlotRuntime>(__func__);
}

Slot::~Slot()
{
  MEM_delete(this->runtime);
}

void Slot::blend_read_post()
{
  BLI_assert(!this->runtime);
  this->runtime = MEM_new<SlotRuntime>(__func__);
}

bool Slot::is_suitable_for(const ID &animated_id) const
{
  if (!this->has_idtype()) {
    /* Without specific ID type set, this Slot can animate any ID. */
    return true;
  }

  /* Check that the ID type is compatible with this slot. */
  const int animated_idtype = GS(animated_id.name);
  return this->idtype == animated_idtype;
}

bool Slot::has_idtype() const
{
  return this->idtype != 0;
}

Slot::Flags Slot::flags() const
{
  return static_cast<Slot::Flags>(this->slot_flags);
}
bool Slot::is_expanded() const
{
  return this->slot_flags & uint8_t(Flags::Expanded);
}
void Slot::set_expanded(const bool expanded)
{
  if (expanded) {
    this->slot_flags |= uint8_t(Flags::Expanded);
  }
  else {
    this->slot_flags &= ~uint8_t(Flags::Expanded);
  }
}

bool Slot::is_selected() const
{
  return this->slot_flags & uint8_t(Flags::Selected);
}
void Slot::set_selected(const bool selected)
{
  if (selected) {
    this->slot_flags |= uint8_t(Flags::Selected);
  }
  else {
    this->slot_flags &= ~uint8_t(Flags::Selected);
  }
}

bool Slot::is_active() const
{
  return this->slot_flags & uint8_t(Flags::Active);
}
void Slot::set_active(const bool active)
{
  if (active) {
    this->slot_flags |= uint8_t(Flags::Active);
  }
  else {
    this->slot_flags &= ~uint8_t(Flags::Active);
  }
}

Span<ID *> Slot::users(Main &bmain) const
{
  if (bmain.is_action_slot_to_id_map_dirty) {
    internal::rebuild_slot_user_cache(bmain);
  }
  BLI_assert(this->runtime);
  return this->runtime->users.as_span();
}

Vector<ID *> Slot::runtime_users()
{
  BLI_assert_msg(this->runtime, "Slot::runtime should always be allocated");
  return this->runtime->users;
}

void Slot::users_add(ID &animated_id)
{
  BLI_assert(this->runtime);
  this->runtime->users.append_non_duplicates(&animated_id);
}

void Slot::users_remove(ID &animated_id)
{
  BLI_assert(this->runtime);
  Vector<ID *> &users = this->runtime->users;

  const int64_t vector_index = users.first_index_of_try(&animated_id);
  if (vector_index < 0) {
    return;
  }

  users.remove_and_reorder(vector_index);
}

void Slot::users_invalidate(Main &bmain)
{
  bmain.is_action_slot_to_id_map_dirty = true;
}

std::string Slot::name_prefix_for_idtype() const
{
  if (!this->has_idtype()) {
    return slot_unbound_prefix;
  }

  char name[3] = {0};
  *reinterpret_cast<short *>(name) = this->idtype;
  return name;
}

StringRefNull Slot::name_without_prefix() const
{
  BLI_assert(StringRef(this->name).size() >= name_length_min);

  /* Avoid accessing an uninitialized part of the string accidentally. */
  if (this->name[0] == '\0' || this->name[1] == '\0') {
    return "";
  }
  return this->name + 2;
}

void Slot::name_ensure_prefix()
{
  BLI_assert(StringRef(this->name).size() >= name_length_min);

  if (StringRef(this->name).size() < 2) {
    /* The code below would overwrite the trailing 0-byte. */
    this->name[2] = '\0';
  }

  if (!this->has_idtype()) {
    /* A zero idtype is not going to convert to a two-character string, so we
     * need to explicitly assign the default prefix. */
    this->name[0] = slot_unbound_prefix[0];
    this->name[1] = slot_unbound_prefix[1];
    return;
  }

  *reinterpret_cast<short *>(this->name) = this->idtype;
}

/* ----- Functions  ----------- */

Action &action_add(Main &bmain, const StringRefNull name)
{
  bAction *dna_action = BKE_action_add(&bmain, name.c_str());
  id_us_clear_real(&dna_action->id);
  return dna_action->wrap();
}

bool assign_action(bAction *action, ID &animated_id)
{
  AnimData *adt = BKE_animdata_ensure_id(&animated_id);
  if (!adt) {
    return false;
  }
  return assign_action(action, {animated_id, *adt});
}

bool assign_action(bAction *action, const OwnedAnimData owned_adt)
{
  if (!BKE_animdata_action_editable(&owned_adt.adt)) {
    /* Cannot remove, otherwise things turn to custard. */
    BKE_report(nullptr, RPT_ERROR, "Cannot change action, as it is still being edited in NLA");
    return false;
  }

  return generic_assign_action(owned_adt.owner_id,
                               action,
                               owned_adt.adt.action,
                               owned_adt.adt.slot_handle,
                               owned_adt.adt.slot_name);
}

bool assign_tmpaction(bAction *action, const OwnedAnimData owned_adt)
{
  return generic_assign_action(owned_adt.owner_id,
                               action,
                               owned_adt.adt.tmpact,
                               owned_adt.adt.tmp_slot_handle,
                               owned_adt.adt.tmp_slot_name);
}

bool unassign_action(ID &animated_id)
{
  return assign_action(nullptr, animated_id);
}

bool unassign_action(OwnedAnimData owned_adt)
{
  return assign_action(nullptr, owned_adt);
}

Slot *assign_action_ensure_slot_for_keying(Action &action, ID &animated_id)
{
  Slot *slot;

  /* Find a suitable slot, but be stricter when to allow searching by name than
   * action.find_suitable_slot_for(animated_id). */
  {
    AnimData *adt = BKE_animdata_from_id(&animated_id);

    if (adt && adt->action == &action) {
      /* The slot handle is only valid when this action is already assigned.
       * Otherwise it's meaningless. */
      slot = action.slot_for_handle(adt->slot_handle);

      /* If this Action is already assigned, a search by name is inappropriate, as it might
       * re-assign an intentionally-unassigned slot. */
    }
    else {
      /* Try the slot name from the AnimData, if it is set. */
      if (adt && adt->slot_name[0]) {
        slot = action.slot_find_by_name(adt->slot_name);
      }
      else {
        /* As a last resort, search for the ID name. */
        slot = action.slot_find_by_name(animated_id.name);
      }
    }
  }

  if (!slot || !slot->is_suitable_for(animated_id)) {
    slot = &action.slot_add_for_id(animated_id);
  }

  /* Only try to assign the Action to the ID if it is not already assigned.
   * Assignment can fail when the ID is in NLA Tweak mode. */
  const std::optional<std::pair<Action *, Slot *>> assigned = get_action_slot_pair(animated_id);
  const bool is_correct_action = assigned && assigned->first == &action;
  if (!is_correct_action && !assign_action(&action, animated_id)) {
    return nullptr;
  }

  const bool is_correct_slot = assigned && assigned->second == slot;
  if (!is_correct_slot && assign_action_slot(slot, animated_id) != ActionSlotAssignmentResult::OK)
  {
    /* This should never happen, as a few lines above a new slot is created for
     * this ID if the found one wasn't deemed suitable. */
    BLI_assert_unreachable();
    return nullptr;
  }

  return slot;
}

static bool is_id_using_action_slot(const ID &animated_id,
                                    const Action &action,
                                    const slot_handle_t slot_handle)
{
  auto visit_action_use = [&](const Action &used_action, slot_handle_t used_slot_handle) -> bool {
    const bool is_used = (&used_action == &action && used_slot_handle == slot_handle);
    return !is_used; /* Stop searching when we found a use of this Action+Slot. */
  };

  const bool looped_until_end = foreach_action_slot_use(animated_id, visit_action_use);
  return !looped_until_end;
}

bool generic_assign_action(ID &animated_id,
                           bAction *action_to_assign,
                           bAction *&action_ptr_ref,
                           slot_handle_t &slot_handle_ref,
                           char *slot_name)
{
  BLI_assert(slot_name);

  if (action_to_assign && legacy::action_treat_as_legacy(*action_to_assign)) {
    /* Check that the Action is suitable for this ID type.
     * This is only necessary for legacy Actions. */
    if (!BKE_animdata_action_ensure_idroot(&animated_id, action_to_assign)) {
      BKE_reportf(
          nullptr,
          RPT_ERROR,
          "Could not set action '%s' to animate ID '%s', as it does not have suitably rooted "
          "paths for this purpose",
          action_to_assign->id.name + 2,
          animated_id.name);
      return false;
    }
  }

  /* Un-assign any previously-assigned Action first. */
  if (action_ptr_ref) {
    /* Un-assign the slot. This will always succeed, so no need to check the result. */
    if (slot_handle_ref != Slot::unassigned) {
      const ActionSlotAssignmentResult result = generic_assign_action_slot(
          nullptr, animated_id, action_ptr_ref, slot_handle_ref, slot_name);
      BLI_assert(result == ActionSlotAssignmentResult::OK);
      UNUSED_VARS_NDEBUG(result);
    }

    /* Un-assign the Action itself. */
    id_us_min(&action_ptr_ref->id);
    action_ptr_ref = nullptr;
  }

  if (!action_to_assign) {
    /* Un-assigning was the point, so the work is done. */
    return true;
  }

  /* Assign the new Action. */
  action_ptr_ref = action_to_assign;
  id_us_plus(&action_ptr_ref->id);

  /* Assign the slot. Legacy Actions do not have slots, so for those `slot` will always be
   * `nullptr`, which is perfectly acceptable for generic_assign_action_slot(). */
  Slot *slot = action_to_assign->wrap().find_suitable_slot_for(animated_id);
  const ActionSlotAssignmentResult result = generic_assign_action_slot(
      slot, animated_id, action_ptr_ref, slot_handle_ref, slot_name);
  BLI_assert(result == ActionSlotAssignmentResult::OK);
  UNUSED_VARS_NDEBUG(result);

  return true;
}

ActionSlotAssignmentResult generic_assign_action_slot(Slot *slot_to_assign,
                                                      ID &animated_id,
                                                      bAction *&action_ptr_ref,
                                                      slot_handle_t &slot_handle_ref,
                                                      char *slot_name)
{
  BLI_assert(slot_name);
  if (!action_ptr_ref) {
    /* No action assigned yet, so no way to assign a slot. */
    return ActionSlotAssignmentResult::MissingAction;
  }

  Action &action = action_ptr_ref->wrap();

  /* Check that the slot can actually be assigned. */
  if (slot_to_assign) {
    if (!action.slots().contains(slot_to_assign)) {
      return ActionSlotAssignmentResult::SlotNotFromAction;
    }

    if (!slot_to_assign->is_suitable_for(animated_id)) {
      return ActionSlotAssignmentResult::SlotNotSuitable;
    }
  }

  Slot *slot_to_unassign = action.slot_for_handle(slot_handle_ref);

  /* If there was a previously-assigned slot, unassign it first. */
  slot_handle_ref = Slot::unassigned;
  if (slot_to_unassign) {
    /* Make sure that the stored Slot name is up to date. The slot name might have
     * changed in a way that wasn't copied into the ADT yet (for example when the
     * Action is linked from another file), so better copy the name to be sure
     * that it can be transparently reassigned later.
     *
     * TODO: Replace this with a BLI_assert() that the name is as expected, and "simply" ensure
     * this name is always correct. */
    BLI_strncpy_utf8(slot_name, slot_to_unassign->name, Slot::name_length_max);

    /* If this was the last use of this slot, remove this ID from its users. */
    if (!is_id_using_action_slot(animated_id, action, slot_to_unassign->handle)) {
      slot_to_unassign->users_remove(animated_id);
    }
  }

  if (!slot_to_assign) {
    return ActionSlotAssignmentResult::OK;
  }

  action.slot_setup_for_id(*slot_to_assign, animated_id);
  slot_handle_ref = slot_to_assign->handle;
  BLI_strncpy_utf8(slot_name, slot_to_assign->name, Slot::name_length_max);
  slot_to_assign->users_add(animated_id);

  return ActionSlotAssignmentResult::OK;
}

ActionSlotAssignmentResult generic_assign_action_slot_handle(slot_handle_t slot_handle_to_assign,
                                                             ID &animated_id,
                                                             bAction *&action_ptr_ref,
                                                             slot_handle_t &slot_handle_ref,
                                                             char *slot_name)
{
  if (slot_handle_to_assign == Slot::unassigned && !action_ptr_ref) {
    /* No Action assigned, so no slot was used anyway. Just blindly assign the
     * 'unassigned' handle. */
    slot_handle_ref = Slot::unassigned;
    return ActionSlotAssignmentResult::OK;
  }

  if (!action_ptr_ref) {
    /* No Action to verify the slot handle is valid. As the slot handle will be
     * completely ignored when re-assigning an Action, better to refuse setting
     * it altogether. This will make bugs more obvious. */
    return ActionSlotAssignmentResult::MissingAction;
  }

  Slot *slot = action_ptr_ref->wrap().slot_for_handle(slot_handle_to_assign);
  return generic_assign_action_slot(slot, animated_id, action_ptr_ref, slot_handle_ref, slot_name);
}

bool is_action_assignable_to(const bAction *dna_action, const ID_Type id_code)
{
  if (!dna_action) {
    /* Clearing the Action is always possible. */
    return true;
  }

  if (dna_action->idroot == 0) {
    /* This is either a never-assigned legacy action, or a layered action. In
     * any case, it can be assigned to any ID. */
    return true;
  }

  const animrig::Action &action = dna_action->wrap();
  if (legacy::action_treat_as_legacy(action)) {
    /* Legacy Actions can only be assigned if their idroot matches. Empty
     * Actions are considered both 'layered' and 'legacy' at the same time,
     * hence this condition checks for 'not layered' rather than 'legacy'. */
    return action.idroot == id_code;
  }

  return true;
}

ActionSlotAssignmentResult assign_action_slot(Slot *slot_to_assign, ID &animated_id)
{
  AnimData *adt = BKE_animdata_from_id(&animated_id);
  if (!adt) {
    return ActionSlotAssignmentResult::MissingAction;
  }

  return generic_assign_action_slot(
      slot_to_assign, animated_id, adt->action, adt->slot_handle, adt->slot_name);
}

ActionSlotAssignmentResult assign_action_and_slot(Action *action,
                                                  Slot *slot_to_assign,
                                                  ID &animated_id)
{
  if (!assign_action(action, animated_id)) {
    return ActionSlotAssignmentResult::MissingAction;
  }
  return assign_action_slot(slot_to_assign, animated_id);
}

ActionSlotAssignmentResult assign_tmpaction_and_slot_handle(bAction *action,
                                                            const slot_handle_t slot_handle,
                                                            const OwnedAnimData owned_adt)
{
  if (!assign_tmpaction(action, owned_adt)) {
    return ActionSlotAssignmentResult::MissingAction;
  }
  return generic_assign_action_slot_handle(slot_handle,
                                           owned_adt.owner_id,
                                           owned_adt.adt.tmpact,
                                           owned_adt.adt.tmp_slot_handle,
                                           owned_adt.adt.tmp_slot_name);
}

/* TODO: rename to get_action(). */
Action *get_action(ID &animated_id)
{
  AnimData *adt = BKE_animdata_from_id(&animated_id);
  if (!adt) {
    return nullptr;
  }
  if (!adt->action) {
    return nullptr;
  }
  return &adt->action->wrap();
}

std::optional<std::pair<Action *, Slot *>> get_action_slot_pair(ID &animated_id)
{
  AnimData *adt = BKE_animdata_from_id(&animated_id);
  if (!adt || !adt->action) {
    /* Not animated by any Action. */
    return std::nullopt;
  }

  Action &action = adt->action->wrap();
  Slot *slot = action.slot_for_handle(adt->slot_handle);
  if (!slot) {
    /* Will not receive any animation from this Action. */
    return std::nullopt;
  }

  return std::make_pair(&action, slot);
}

/* ----- ActionStrip implementation ----------- */

Strip &Strip::create(Action &owning_action, const Strip::Type type)
{
  /* Create the strip. */
  ActionStrip *strip = MEM_cnew<ActionStrip>(__func__);
  memcpy(strip, DNA_struct_default_get(ActionStrip), sizeof(*strip));
  strip->strip_type = int8_t(type);

  /* Create the strip's data on the owning Action. */
  switch (type) {
    case Strip::Type::Keyframe: {
      StripKeyframeData *strip_data = MEM_new<StripKeyframeData>(__func__);
      strip->data_index = owning_action.strip_keyframe_data_append(strip_data);
      break;
    }
  }

  /* This can happen if someone forgets to add a strip type in the `switch`
   * above, or if someone is evil and passes an invalid strip type to this
   * function. */
  BLI_assert_msg(strip->data_index != -1, "Newly created strip has no data.");

  return strip->wrap();
}

bool Strip::is_infinite() const
{
  return this->frame_start == -std::numeric_limits<float>::infinity() &&
         this->frame_end == std::numeric_limits<float>::infinity();
}

bool Strip::contains_frame(const float frame_time) const
{
  return this->frame_start <= frame_time && frame_time <= this->frame_end;
}

bool Strip::is_last_frame(const float frame_time) const
{
  /* Maybe this needs a more advanced equality check. Implement that when
   * we have an actual example case that breaks. */
  return this->frame_end == frame_time;
}

void Strip::resize(const float frame_start, const float frame_end)
{
  BLI_assert(frame_start <= frame_end);
  BLI_assert_msg(frame_start < std::numeric_limits<float>::infinity(),
                 "only the end frame can be at positive infinity");
  BLI_assert_msg(frame_end > -std::numeric_limits<float>::infinity(),
                 "only the start frame can be at negative infinity");
  this->frame_start = frame_start;
  this->frame_end = frame_end;
}

template<>
const StripKeyframeData &Strip::data<StripKeyframeData>(const Action &owning_action) const
{
  BLI_assert(this->type() == StripKeyframeData::TYPE);

  return *owning_action.strip_keyframe_data()[this->data_index];
}
template<> StripKeyframeData &Strip::data<StripKeyframeData>(Action &owning_action)
{
  BLI_assert(this->type() == StripKeyframeData::TYPE);

  return *owning_action.strip_keyframe_data()[this->data_index];
}

void Strip::slot_data_remove(Action &owning_action, const slot_handle_t slot_handle)
{
  switch (this->type()) {
    case Type::Keyframe:
      this->data<StripKeyframeData>(owning_action).slot_data_remove(slot_handle);
  }
}

/* ----- ActionStripKeyframeData implementation ----------- */

StripKeyframeData::StripKeyframeData(const StripKeyframeData &other)
{
  memcpy(this, &other, sizeof(*this));

  this->channelbag_array = MEM_cnew_array<ActionChannelBag *>(other.channelbag_array_num,
                                                              __func__);
  Span<const ChannelBag *> channelbags_src = other.channelbags();
  for (int i : channelbags_src.index_range()) {
    this->channelbag_array[i] = MEM_new<animrig::ChannelBag>(__func__, *other.channelbag(i));
  }
}

StripKeyframeData::~StripKeyframeData()
{
  for (ChannelBag *channelbag_for_slot : this->channelbags()) {
    MEM_delete(channelbag_for_slot);
  }
  MEM_SAFE_FREE(this->channelbag_array);
  this->channelbag_array_num = 0;
}

blender::Span<const ChannelBag *> StripKeyframeData::channelbags() const
{
  return blender::Span<ChannelBag *>{reinterpret_cast<ChannelBag **>(this->channelbag_array),
                                     this->channelbag_array_num};
}
blender::Span<ChannelBag *> StripKeyframeData::channelbags()
{
  return blender::Span<ChannelBag *>{reinterpret_cast<ChannelBag **>(this->channelbag_array),
                                     this->channelbag_array_num};
}
const ChannelBag *StripKeyframeData::channelbag(const int64_t index) const
{
  return &this->channelbag_array[index]->wrap();
}
ChannelBag *StripKeyframeData::channelbag(const int64_t index)
{
  return &this->channelbag_array[index]->wrap();
}
const ChannelBag *StripKeyframeData::channelbag_for_slot(const slot_handle_t slot_handle) const
{
  for (const ChannelBag *channels : this->channelbags()) {
    if (channels->slot_handle == slot_handle) {
      return channels;
    }
  }
  return nullptr;
}
int64_t StripKeyframeData::find_channelbag_index(const ChannelBag &channelbag) const
{
  for (int64_t index = 0; index < this->channelbag_array_num; index++) {
    if (this->channelbag(index) == &channelbag) {
      return index;
    }
  }
  return -1;
}
ChannelBag *StripKeyframeData::channelbag_for_slot(const slot_handle_t slot_handle)
{
  const auto *const_this = const_cast<const StripKeyframeData *>(this);
  const auto *const_channels = const_this->channelbag_for_slot(slot_handle);
  return const_cast<ChannelBag *>(const_channels);
}
const ChannelBag *StripKeyframeData::channelbag_for_slot(const Slot &slot) const
{
  return this->channelbag_for_slot(slot.handle);
}
ChannelBag *StripKeyframeData::channelbag_for_slot(const Slot &slot)
{
  return this->channelbag_for_slot(slot.handle);
}

ChannelBag &StripKeyframeData::channelbag_for_slot_add(const Slot &slot)
{
  BLI_assert_msg(channelbag_for_slot(slot) == nullptr,
                 "Cannot add chans-for-slot for already-registered slot");

  ChannelBag &channels = MEM_new<ActionChannelBag>(__func__)->wrap();
  channels.slot_handle = slot.handle;

  grow_array_and_append<ActionChannelBag *>(
      &this->channelbag_array, &this->channelbag_array_num, &channels);

  return channels;
}

ChannelBag &StripKeyframeData::channelbag_for_slot_ensure(const Slot &slot)
{
  ChannelBag *channel_bag = this->channelbag_for_slot(slot);
  if (channel_bag != nullptr) {
    return *channel_bag;
  }
  return this->channelbag_for_slot_add(slot);
}

static void channelbag_ptr_destructor(ActionChannelBag **dna_channelbag_ptr)
{
  ChannelBag &channelbag = (*dna_channelbag_ptr)->wrap();
  MEM_delete(&channelbag);
};

bool StripKeyframeData::channelbag_remove(ChannelBag &channelbag_to_remove)
{
  const int64_t channelbag_index = this->find_channelbag_index(channelbag_to_remove);
  if (channelbag_index < 0) {
    return false;
  }

  dna::array::remove_index(&this->channelbag_array,
                           &this->channelbag_array_num,
                           nullptr,
                           channelbag_index,
                           channelbag_ptr_destructor);

  return true;
}

void StripKeyframeData::slot_data_remove(const slot_handle_t slot_handle)
{
  ChannelBag *channelbag = this->channelbag_for_slot(slot_handle);
  if (!channelbag) {
    return;
  }
  this->channelbag_remove(*channelbag);
}

const FCurve *ChannelBag::fcurve_find(const FCurveDescriptor fcurve_descriptor) const
{
  return animrig::fcurve_find(this->fcurves(), fcurve_descriptor);
}

FCurve *ChannelBag::fcurve_find(const FCurveDescriptor fcurve_descriptor)
{
  /* Intermediate variable needed to disambiguate const/non-const overloads. */
  Span<FCurve *> fcurves = this->fcurves();
  return animrig::fcurve_find(fcurves, fcurve_descriptor);
}

FCurve &ChannelBag::fcurve_ensure(Main *bmain, const FCurveDescriptor fcurve_descriptor)
{
  if (FCurve *existing_fcurve = this->fcurve_find(fcurve_descriptor)) {
    return *existing_fcurve;
  }
  return this->fcurve_create(bmain, fcurve_descriptor);
}

FCurve *ChannelBag::fcurve_create_unique(Main *bmain, FCurveDescriptor fcurve_descriptor)
{
  if (this->fcurve_find(fcurve_descriptor)) {
    return nullptr;
  }
  return &this->fcurve_create(bmain, fcurve_descriptor);
}

FCurve &ChannelBag::fcurve_create(Main *bmain, FCurveDescriptor fcurve_descriptor)
{
  FCurve *new_fcurve = create_fcurve_for_channel(fcurve_descriptor);

  if (this->fcurve_array_num == 0) {
    new_fcurve->flag |= FCURVE_ACTIVE; /* First curve is added active. */
  }

  bActionGroup *group = fcurve_descriptor.channel_group.has_value() ?
                            &this->channel_group_ensure(*fcurve_descriptor.channel_group) :
                            nullptr;
  const int insert_index = group ? group->fcurve_range_start + group->fcurve_range_length :
                                   this->fcurve_array_num;
  BLI_assert(insert_index <= this->fcurve_array_num);

  grow_array_and_insert(&this->fcurve_array, &this->fcurve_array_num, insert_index, new_fcurve);
  if (group) {
    group->fcurve_range_length += 1;
    this->restore_channel_group_invariants();
  }

  if (bmain) {
    DEG_relations_tag_update(bmain);
  }

  return *new_fcurve;
}

void ChannelBag::fcurve_append(FCurve &fcurve)
{
  /* Appended F-Curves don't belong to any group yet, so better make sure their
   * group pointer reflects that. */
  fcurve.grp = nullptr;

  grow_array_and_append(&this->fcurve_array, &this->fcurve_array_num, &fcurve);
}

static void fcurve_ptr_destructor(FCurve **fcurve_ptr)
{
  BKE_fcurve_free(*fcurve_ptr);
};

bool ChannelBag::fcurve_remove(FCurve &fcurve_to_remove)
{
  if (!this->fcurve_detach(fcurve_to_remove)) {
    return false;
  }
  BKE_fcurve_free(&fcurve_to_remove);
  return true;
}

void ChannelBag::fcurve_remove_by_index(const int64_t fcurve_index)
{
  /* Grab the pointer before it's detached, so we can free it after. */
  FCurve *fcurve_to_remove = this->fcurve(fcurve_index);

  this->fcurve_detach_by_index(fcurve_index);

  BKE_fcurve_free(fcurve_to_remove);
}

static void fcurve_ptr_noop_destructor(FCurve ** /*fcurve_ptr*/) {}

bool ChannelBag::fcurve_detach(FCurve &fcurve_to_detach)
{
  const int64_t fcurve_index = this->fcurves().first_index_try(&fcurve_to_detach);
  if (fcurve_index < 0) {
    return false;
  }
  this->fcurve_detach_by_index(fcurve_index);
  return true;
}

void ChannelBag::fcurve_detach_by_index(const int64_t fcurve_index)
{
  BLI_assert(fcurve_index >= 0);
  BLI_assert(fcurve_index < this->fcurve_array_num);

  const int group_index = this->channel_group_containing_index(fcurve_index);
  if (group_index != -1) {
    bActionGroup *group = this->channel_group(group_index);

    group->fcurve_range_length -= 1;
    if (group->fcurve_range_length <= 0) {
      const int group_index = this->channel_groups().first_index_try(group);
      this->channel_group_remove_raw(group_index);
    }
  }

  dna::array::remove_index(&this->fcurve_array,
                           &this->fcurve_array_num,
                           nullptr,
                           fcurve_index,
                           fcurve_ptr_noop_destructor);

  this->restore_channel_group_invariants();

  /* As an optimization, this function could call `DEG_relations_tag_update(bmain)` to prune any
   * relationships that are now no longer necessary. This is not needed for correctness of the
   * depsgraph evaluation results though. */
}

void ChannelBag::fcurve_move(FCurve &fcurve, int to_fcurve_index)
{
  BLI_assert(to_fcurve_index >= 0 && to_fcurve_index < this->fcurves().size());

  const int fcurve_index = this->fcurves().first_index_try(&fcurve);
  BLI_assert_msg(fcurve_index >= 0, "FCurve not in this channel bag.");

  array_shift_range(
      this->fcurve_array, this->fcurve_array_num, fcurve_index, fcurve_index + 1, to_fcurve_index);

  this->restore_channel_group_invariants();
}

void ChannelBag::fcurves_clear()
{
  dna::array::clear(&this->fcurve_array, &this->fcurve_array_num, nullptr, fcurve_ptr_destructor);

  /* Since all F-Curves are gone, the groups are all empty. */
  for (bActionGroup *group : channel_groups()) {
    group->fcurve_range_start = 0;
    group->fcurve_range_length = 0;
  }
}

SingleKeyingResult StripKeyframeData::keyframe_insert(Main *bmain,
                                                      const Slot &slot,
                                                      const FCurveDescriptor fcurve_descriptor,
                                                      const float2 time_value,
                                                      const KeyframeSettings &settings,
                                                      const eInsertKeyFlags insert_key_flags)
{
  /* Get the fcurve, or create one if it doesn't exist and the keying flags
   * allow. */
  FCurve *fcurve = nullptr;
  if (key_insertion_may_create_fcurve(insert_key_flags)) {
    fcurve = &this->channelbag_for_slot_ensure(slot).fcurve_ensure(bmain, fcurve_descriptor);
  }
  else {
    ChannelBag *channels = this->channelbag_for_slot(slot);
    if (channels != nullptr) {
      fcurve = channels->fcurve_find(fcurve_descriptor);
    }
  }

  if (!fcurve) {
    std::fprintf(stderr,
                 "FCurve %s[%d] for slot %s was not created due to either the Only Insert "
                 "Available setting or Replace keyframing mode.\n",
                 fcurve_descriptor.rna_path.c_str(),
                 fcurve_descriptor.array_index,
                 slot.name);
    return SingleKeyingResult::CANNOT_CREATE_FCURVE;
  }

  if (!BKE_fcurve_is_keyframable(fcurve)) {
    /* TODO: handle this properly, in a way that can be communicated to the user. */
    std::fprintf(stderr,
                 "FCurve %s[%d] for slot %s doesn't allow inserting keys.\n",
                 fcurve_descriptor.rna_path.c_str(),
                 fcurve_descriptor.array_index,
                 slot.name);
    return SingleKeyingResult::FCURVE_NOT_KEYFRAMEABLE;
  }

  const SingleKeyingResult insert_vert_result = insert_vert_fcurve(
      fcurve, time_value, settings, insert_key_flags);

  if (insert_vert_result != SingleKeyingResult::SUCCESS) {
    std::fprintf(stderr,
                 "Could not insert key into FCurve %s[%d] for slot %s.\n",
                 fcurve_descriptor.rna_path.c_str(),
                 fcurve_descriptor.array_index,
                 slot.name);
    return insert_vert_result;
  }

  return SingleKeyingResult::SUCCESS;
}

/* ActionChannelBag implementation. */

ChannelBag::ChannelBag(const ChannelBag &other)
{
  this->slot_handle = other.slot_handle;

  this->fcurve_array_num = other.fcurve_array_num;
  this->fcurve_array = MEM_cnew_array<FCurve *>(other.fcurve_array_num, __func__);
  for (int i = 0; i < other.fcurve_array_num; i++) {
    const FCurve *fcu_src = other.fcurve_array[i];
    this->fcurve_array[i] = BKE_fcurve_copy(fcu_src);
  }

  this->group_array_num = other.group_array_num;
  this->group_array = MEM_cnew_array<bActionGroup *>(other.group_array_num, __func__);
  for (int i = 0; i < other.group_array_num; i++) {
    const bActionGroup *group_src = other.group_array[i];
    this->group_array[i] = static_cast<bActionGroup *>(MEM_dupallocN(group_src));
    this->group_array[i]->channel_bag = this;
  }

  /* BKE_fcurve_copy() resets the FCurve's group pointer. Which is good, because the groups are
   * duplicated too. This sets the group pointers to the correct values. */
  this->restore_channel_group_invariants();
}

ChannelBag::~ChannelBag()
{
  for (FCurve *fcu : this->fcurves()) {
    BKE_fcurve_free(fcu);
  }
  MEM_SAFE_FREE(this->fcurve_array);
  this->fcurve_array_num = 0;

  for (bActionGroup *group : this->channel_groups()) {
    MEM_SAFE_FREE(group);
  }
  MEM_SAFE_FREE(this->group_array);
  this->group_array_num = 0;
}

blender::Span<const FCurve *> ChannelBag::fcurves() const
{
  return blender::Span<FCurve *>{this->fcurve_array, this->fcurve_array_num};
}
blender::Span<FCurve *> ChannelBag::fcurves()
{
  return blender::Span<FCurve *>{this->fcurve_array, this->fcurve_array_num};
}
const FCurve *ChannelBag::fcurve(const int64_t index) const
{
  return this->fcurve_array[index];
}
FCurve *ChannelBag::fcurve(const int64_t index)
{
  return this->fcurve_array[index];
}

blender::Span<const bActionGroup *> ChannelBag::channel_groups() const
{
  return blender::Span<bActionGroup *>{this->group_array, this->group_array_num};
}
blender::Span<bActionGroup *> ChannelBag::channel_groups()
{
  return blender::Span<bActionGroup *>{this->group_array, this->group_array_num};
}
const bActionGroup *ChannelBag::channel_group(const int64_t index) const
{
  BLI_assert(index < this->group_array_num);
  return this->group_array[index];
}
bActionGroup *ChannelBag::channel_group(const int64_t index)
{
  BLI_assert(index < this->group_array_num);
  return this->group_array[index];
}

const bActionGroup *ChannelBag::channel_group_find(const StringRef name) const
{
  for (const bActionGroup *group : this->channel_groups()) {
    if (name == StringRef{group->name}) {
      return group;
    }
  }

  return nullptr;
}

bActionGroup *ChannelBag::channel_group_find(const StringRef name)
{
  /* Intermediate variable needed to disambiguate const/non-const overloads. */
  Span<bActionGroup *> groups = this->channel_groups();
  for (bActionGroup *group : groups) {
    if (name == StringRef{group->name}) {
      return group;
    }
  }

  return nullptr;
}

int ChannelBag::channel_group_containing_index(const int fcurve_array_index)
{
  int i = 0;
  for (const bActionGroup *group : this->channel_groups()) {
    if (fcurve_array_index >= group->fcurve_range_start &&
        fcurve_array_index < (group->fcurve_range_start + group->fcurve_range_length))
    {
      return i;
    }
    i++;
  }

  return -1;
}

bActionGroup &ChannelBag::channel_group_create(StringRefNull name)
{
  bActionGroup *new_group = static_cast<bActionGroup *>(
      MEM_callocN(sizeof(bActionGroup), __func__));

  /* Find the end fcurve index of the current channel groups, to be used as the
   * start of the new channel group. */
  int fcurve_index = 0;
  const int length = this->channel_groups().size();
  if (length > 0) {
    const bActionGroup *last = this->channel_group(length - 1);
    fcurve_index = last->fcurve_range_start + last->fcurve_range_length;
  }
  new_group->fcurve_range_start = fcurve_index;

  new_group->channel_bag = this;

  /* Make it selected. */
  new_group->flag = AGRP_SELECTED;

  /* Ensure it has a unique name.
   *
   * Note that this only happens here (upon creation). The user can later rename
   * groups to have duplicate names. This is stupid, but it's how the legacy
   * system worked, and at the time of writing this code we're just trying to
   * match that system's behavior, even when it's goofy.*/
  std::string unique_name = BLI_uniquename_cb(
      [&](const StringRef name) {
        for (const bActionGroup *group : this->channel_groups()) {
          if (STREQ(group->name, name.data())) {
            return true;
          }
        }
        return false;
      },
      '.',
      name[0] == '\0' ? DATA_("Group") : name);

  STRNCPY_UTF8(new_group->name, unique_name.c_str());

  grow_array_and_append(&this->group_array, &this->group_array_num, new_group);

  return *new_group;
}

bActionGroup &ChannelBag::channel_group_ensure(StringRefNull name)
{
  bActionGroup *group = this->channel_group_find(name);
  if (group) {
    return *group;
  }

  return this->channel_group_create(name);
}

bool ChannelBag::channel_group_remove(bActionGroup &group)
{
  const int group_index = this->channel_groups().first_index_try(&group);
  if (group_index == -1) {
    return false;
  }

  /* Move the group's fcurves to just past the end of where the grouped
   * fcurves will be after this group is removed. */
  const bActionGroup *last_group = this->channel_groups().last();
  BLI_assert(last_group != nullptr);
  const int to_index = last_group->fcurve_range_start + last_group->fcurve_range_length -
                       group.fcurve_range_length;
  array_shift_range(this->fcurve_array,
                    this->fcurve_array_num,
                    group.fcurve_range_start,
                    group.fcurve_range_start + group.fcurve_range_length,
                    to_index);

  this->channel_group_remove_raw(group_index);
  this->restore_channel_group_invariants();

  return true;
}

void ChannelBag::channel_group_move(bActionGroup &group, const int to_group_index)
{
  BLI_assert(to_group_index >= 0 && to_group_index < this->channel_groups().size());

  const int group_index = this->channel_groups().first_index_try(&group);
  BLI_assert_msg(group_index >= 0, "Group not in this channel bag.");

  /* Shallow copy, to track which fcurves should be moved in the second step. */
  const bActionGroup pre_move_group = group;

  /* First we move the group to its new position. The call to
   * `restore_channel_group_invariants()` is necessary to update the group's
   * fcurve range (as well as the ranges of the other groups) to match its new
   * position in the group array. */
  array_shift_range(
      this->group_array, this->group_array_num, group_index, group_index + 1, to_group_index);
  this->restore_channel_group_invariants();

  /* Move the fcurves that were part of `group` (as recorded in
   *`pre_move_group`) to their new positions (now in `group`) so that they're
   * part of `group` again. */
  array_shift_range(this->fcurve_array,
                    this->fcurve_array_num,
                    pre_move_group.fcurve_range_start,
                    pre_move_group.fcurve_range_start + pre_move_group.fcurve_range_length,
                    group.fcurve_range_start);
  this->restore_channel_group_invariants();
}

void ChannelBag::channel_group_remove_raw(const int group_index)
{
  BLI_assert(group_index >= 0 && group_index < this->channel_groups().size());

  MEM_SAFE_FREE(this->group_array[group_index]);
  shrink_array_and_remove(&this->group_array, &this->group_array_num, group_index);
}

void ChannelBag::restore_channel_group_invariants()
{
  /* Shift channel groups. */
  {
    int start_index = 0;
    for (bActionGroup *group : this->channel_groups()) {
      group->fcurve_range_start = start_index;
      start_index += group->fcurve_range_length;
    }

    /* Double-check that this didn't push any of the groups off the end of the
     * fcurve array. */
    BLI_assert(start_index <= this->fcurve_array_num);
  }

  /* Recompute fcurves' group pointers. */
  {
    for (FCurve *fcurve : this->fcurves()) {
      fcurve->grp = nullptr;
    }
    for (bActionGroup *group : this->channel_groups()) {
      for (FCurve *fcurve : group->wrap().fcurves()) {
        fcurve->grp = group;
      }
    }
  }
}

bool ChannelGroup::is_legacy() const
{
  return this->channel_bag == nullptr;
}

Span<FCurve *> ChannelGroup::fcurves()
{
  BLI_assert(!this->is_legacy());

  if (this->fcurve_range_length == 0) {
    return {};
  }

  return this->channel_bag->wrap().fcurves().slice(this->fcurve_range_start,
                                                   this->fcurve_range_length);
}

Span<const FCurve *> ChannelGroup::fcurves() const
{
  BLI_assert(!this->is_legacy());

  if (this->fcurve_range_length == 0) {
    return {};
  }

  return this->channel_bag->wrap().fcurves().slice(this->fcurve_range_start,
                                                   this->fcurve_range_length);
}

/* Utility function implementations. */

const animrig::ChannelBag *channelbag_for_action_slot(const Action &action,
                                                      const slot_handle_t slot_handle)
{
  assert_baklava_phase_1_invariants(action);

  if (slot_handle == Slot::unassigned) {
    return nullptr;
  }

  for (const animrig::Layer *layer : action.layers()) {
    for (const animrig::Strip *strip : layer->strips()) {
      switch (strip->type()) {
        case animrig::Strip::Type::Keyframe: {
          const animrig::StripKeyframeData &strip_data = strip->data<animrig::StripKeyframeData>(
              action);
          const animrig::ChannelBag *bag = strip_data.channelbag_for_slot(slot_handle);
          if (bag) {
            return bag;
          }
        }
      }
    }
  }

  return nullptr;
}

animrig::ChannelBag *channelbag_for_action_slot(Action &action, const slot_handle_t slot_handle)
{
  const animrig::ChannelBag *const_bag = channelbag_for_action_slot(
      const_cast<const Action &>(action), slot_handle);
  return const_cast<animrig::ChannelBag *>(const_bag);
}

Span<FCurve *> fcurves_for_action_slot(Action &action, const slot_handle_t slot_handle)
{
  BLI_assert(action.is_action_layered());
  assert_baklava_phase_1_invariants(action);
  animrig::ChannelBag *bag = channelbag_for_action_slot(action, slot_handle);
  if (!bag) {
    return {};
  }
  return bag->fcurves();
}

Span<const FCurve *> fcurves_for_action_slot(const Action &action, const slot_handle_t slot_handle)
{
  BLI_assert(action.is_action_layered());
  assert_baklava_phase_1_invariants(action);
  const animrig::ChannelBag *bag = channelbag_for_action_slot(action, slot_handle);
  if (!bag) {
    return {};
  }
  return bag->fcurves();
}

FCurve *fcurve_find_in_action(bAction *act, FCurveDescriptor fcurve_descriptor)
{
  if (act == nullptr) {
    return nullptr;
  }

  Action &action = act->wrap();
  if (action.is_action_legacy()) {
    return BKE_fcurve_find(
        &act->curves, fcurve_descriptor.rna_path.c_str(), fcurve_descriptor.array_index);
  }

  assert_baklava_phase_1_invariants(action);
  Layer *layer = action.layer(0);
  if (!layer) {
    return nullptr;
  }
  Strip *strip = layer->strip(0);
  if (!strip) {
    return nullptr;
  }

  StripKeyframeData &strip_data = strip->data<StripKeyframeData>(action);

  for (ChannelBag *channelbag : strip_data.channelbags()) {
    FCurve *fcu = channelbag->fcurve_find(fcurve_descriptor);
    if (fcu) {
      return fcu;
    }
  }

  return nullptr;
}

FCurve *fcurve_find_in_assigned_slot(AnimData &adt, FCurveDescriptor fcurve_descriptor)
{
  return fcurve_find_in_action_slot(adt.action, adt.slot_handle, fcurve_descriptor);
}

FCurve *fcurve_find_in_action_slot(bAction *act,
                                   const slot_handle_t slot_handle,
                                   FCurveDescriptor fcurve_descriptor)
{
  if (act == nullptr) {
    return nullptr;
  }

  Action &action = act->wrap();
  if (action.is_action_legacy()) {
    return BKE_fcurve_find(
        &act->curves, fcurve_descriptor.rna_path.c_str(), fcurve_descriptor.array_index);
  }

  ChannelBag *cbag = channelbag_for_action_slot(action, slot_handle);
  if (!cbag) {
    return nullptr;
  }
  return cbag->fcurve_find(fcurve_descriptor);
}

bool fcurve_matches_collection_path(const FCurve &fcurve,
                                    const StringRefNull collection_rna_path,
                                    const StringRefNull data_name)
{
  BLI_assert(!collection_rna_path.is_empty());

  const size_t quoted_name_size = data_name.size() + 1;
  char *quoted_name = static_cast<char *>(alloca(quoted_name_size));

  if (!fcurve.rna_path) {
    return false;
  }
  /* Skipping names longer than `quoted_name_size` is OK since we're after an exact match. */
  if (!BLI_str_quoted_substr(
          fcurve.rna_path, collection_rna_path.c_str(), quoted_name, quoted_name_size))
  {
    return false;
  }
  if (quoted_name != data_name) {
    return false;
  }

  return true;
}

Vector<FCurve *> fcurves_in_action_slot_filtered(bAction *act,
                                                 const slot_handle_t slot_handle,
                                                 FunctionRef<bool(const FCurve &fcurve)> predicate)
{
  BLI_assert(act);

  Vector<FCurve *> found;

  foreach_fcurve_in_action_slot(act->wrap(), slot_handle, [&](FCurve &fcurve) {
    if (predicate(fcurve)) {
      found.append(&fcurve);
    }
  });

  return found;
}

Vector<FCurve *> fcurves_in_span_filtered(Span<FCurve *> fcurves,
                                          FunctionRef<bool(const FCurve &fcurve)> predicate)
{
  Vector<FCurve *> found;

  for (FCurve *fcurve : fcurves) {
    if (predicate(*fcurve)) {
      found.append(fcurve);
    }
  }

  return found;
}

Vector<FCurve *> fcurves_in_listbase_filtered(ListBase /* FCurve * */ fcurves,
                                              FunctionRef<bool(const FCurve &fcurve)> predicate)
{
  Vector<FCurve *> found;

  LISTBASE_FOREACH (FCurve *, fcurve, &fcurves) {
    if (predicate(*fcurve)) {
      found.append(fcurve);
    }
  }

  return found;
}

FCurve *action_fcurve_ensure(Main *bmain,
                             bAction *act,
                             const char group[],
                             PointerRNA *ptr,
                             FCurveDescriptor fcurve_descriptor)
{
  if (act == nullptr) {
    return nullptr;
  }

  if (!animrig::legacy::action_treat_as_legacy(*act)) {
    /* NOTE: for layered actions we require the following:
     *
     * - `ptr` is non-null.
     * - `ptr` has an `owner_id` that already uses `act`.
     *
     * This isn't for any principled reason, but rather is because adding
     * support for layered actions to this function was a fix to make Follow
     * Path animation work properly with layered actions (see PR #124353), and
     * those are the requirements the Follow Path code conveniently met.
     * Moreover those requirements were also already met by the other call sites
     * that potentially call this function with layered actions.
     *
     * Trying to puzzle out what "should" happen when these requirements don't
     * hold, or if this is even the best place to handle the layered action
     * cases at all, was leading to discussion of larger changes than made sense
     * to tackle at that point. */
    Action &action = act->wrap();

    BLI_assert(ptr != nullptr);
    if (ptr == nullptr || ptr->owner_id == nullptr) {
      return nullptr;
    }
    ID &animated_id = *ptr->owner_id;
    BLI_assert(get_action(animated_id) == &action);
    if (get_action(animated_id) != &action) {
      return nullptr;
    }

    /* Ensure the id has an assigned slot. */
    Slot *slot = assign_action_ensure_slot_for_keying(action, animated_id);
    if (!slot) {
      /* This means the ID type is not animatable. */
      return nullptr;
    }

    action.layer_keystrip_ensure();

    assert_baklava_phase_1_invariants(action);
    StripKeyframeData &strip_data = action.layer(0)->strip(0)->data<StripKeyframeData>(action);

    return &strip_data.channelbag_for_slot_ensure(*slot).fcurve_ensure(bmain, fcurve_descriptor);
  }

  /* Try to find f-curve matching for this setting.
   * - add if not found and allowed to add one
   *   TODO: add auto-grouping support? how this works will need to be resolved
   */
  FCurve *fcu = animrig::fcurve_find_in_action(act, fcurve_descriptor);

  if (fcu != nullptr) {
    return fcu;
  }

  /* Determine the property subtype if we can. */
  std::optional<PropertySubType> prop_subtype = std::nullopt;
  if (ptr != nullptr) {
    PropertyRNA *resolved_prop;
    PointerRNA resolved_ptr;
    PointerRNA id_ptr = RNA_id_pointer_create(ptr->owner_id);
    const bool resolved = RNA_path_resolve_property(
        &id_ptr, fcurve_descriptor.rna_path.c_str(), &resolved_ptr, &resolved_prop);
    if (resolved) {
      prop_subtype = RNA_property_subtype(resolved_prop);
    }
  }

  BLI_assert_msg(!fcurve_descriptor.prop_subtype.has_value(),
                 "Did not expect a prop_subtype to be passed in. This is fine, but does need some "
                 "changes to action_fcurve_ensure() to deal with it");
  fcu = create_fcurve_for_channel(
      {fcurve_descriptor.rna_path, fcurve_descriptor.array_index, prop_subtype});

  if (BLI_listbase_is_empty(&act->curves)) {
    fcu->flag |= FCURVE_ACTIVE;
  }

  if (group) {
    bActionGroup *agrp = BKE_action_group_find_name(act, group);

    if (agrp == nullptr) {
      agrp = action_groups_add_new(act, group);

      /* Sync bone group colors if applicable. */
      if (ptr && (ptr->type == &RNA_PoseBone) && ptr->data) {
        const bPoseChannel *pchan = static_cast<const bPoseChannel *>(ptr->data);
        action_group_colors_set_from_posebone(agrp, pchan);
      }
    }

    action_groups_add_channel(act, agrp, fcu);
  }
  else {
    BLI_addtail(&act->curves, fcu);
  }

  /* New f-curve was added, meaning it's possible that it affects
   * dependency graph component which wasn't previously animated.
   */
  DEG_relations_tag_update(bmain);

  return fcu;
}

bool action_fcurve_remove(Action &action, FCurve &fcu)
{
  if (action_fcurve_detach(action, fcu)) {
    BKE_fcurve_free(&fcu);
    return true;
  }

  return false;
}

bool action_fcurve_detach(Action &action, FCurve &fcurve_to_detach)
{
  if (action.is_action_legacy()) {
    return BLI_remlink_safe(&action.curves, &fcurve_to_detach);
  }

  for (Layer *layer : action.layers()) {
    for (Strip *strip : layer->strips()) {
      if (!(strip->type() == Strip::Type::Keyframe)) {
        continue;
      }
      StripKeyframeData &strip_data = strip->data<StripKeyframeData>(action);
      for (ChannelBag *bag : strip_data.channelbags()) {
        const bool is_detached = bag->fcurve_detach(fcurve_to_detach);
        if (is_detached) {
          return true;
        }
      }
    }
  }
  return false;
}

void action_fcurve_attach(Action &action,
                          const slot_handle_t action_slot,
                          FCurve &fcurve_to_attach,
                          std::optional<StringRefNull> group_name)
{
  if (animrig::legacy::action_treat_as_legacy(action)) {
    BLI_addtail(&action.curves, &fcurve_to_attach);
    return;
  }

  Slot *slot = action.slot_for_handle(action_slot);
  BLI_assert(slot);
  if (!slot) {
    printf("Cannot find slot handle %d on Action %s, unable to attach F-Curve %s[%d] to it!\n",
           action_slot,
           action.id.name + 2,
           fcurve_to_attach.rna_path,
           fcurve_to_attach.array_index);
    return;
  }

  action.layer_keystrip_ensure();
  StripKeyframeData &strip_data = action.layer(0)->strip(0)->data<StripKeyframeData>(action);
  ChannelBag &cbag = strip_data.channelbag_for_slot_ensure(*slot);
  cbag.fcurve_append(fcurve_to_attach);

  if (group_name) {
    bActionGroup &group = cbag.channel_group_ensure(*group_name);
    cbag.fcurve_assign_to_channel_group(fcurve_to_attach, group);
  }
}

void action_fcurve_move(Action &action_dst,
                        const slot_handle_t action_slot_dst,
                        Action &action_src,
                        FCurve &fcurve)
{
  /* Store the group name locally, as the group will be removed if this was its
   * last F-Curve. */
  std::optional<std::string> group_name;
  if (fcurve.grp) {
    group_name = fcurve.grp->name;
  }

  const bool is_detached = action_fcurve_detach(action_src, fcurve);
  BLI_assert(is_detached);
  UNUSED_VARS_NDEBUG(is_detached);

  action_fcurve_attach(action_dst, action_slot_dst, fcurve, group_name);
}

void channelbag_fcurves_move(ChannelBag &channelbag_dst, ChannelBag &channelbag_src)
{
  while (!channelbag_src.fcurves().is_empty()) {
    FCurve &fcurve = *channelbag_src.fcurve(0);

    /* Store the group name locally, as the group will be removed if this was its
     * last F-Curve. */
    std::optional<std::string> group_name;
    if (fcurve.grp) {
      group_name = fcurve.grp->name;
    }

    const bool is_detached = channelbag_src.fcurve_detach(fcurve);
    BLI_assert(is_detached);
    UNUSED_VARS_NDEBUG(is_detached);

    channelbag_dst.fcurve_append(fcurve);

    if (group_name) {
      bActionGroup &group = channelbag_dst.channel_group_ensure(*group_name);
      channelbag_dst.fcurve_assign_to_channel_group(fcurve, group);
    }
  }
}

bool ChannelBag::fcurve_assign_to_channel_group(FCurve &fcurve, bActionGroup &to_group)
{
  if (this->channel_groups().first_index_try(&to_group) == -1) {
    return false;
  }

  const int fcurve_index = this->fcurves().first_index_try(&fcurve);
  if (fcurve_index == -1) {
    return false;
  }

  if (fcurve.grp == &to_group) {
    return true;
  }

  /* Remove fcurve from old group, if it belongs to one. */
  if (fcurve.grp != nullptr) {
    fcurve.grp->fcurve_range_length--;
    if (fcurve.grp->fcurve_range_length == 0) {
      const int group_index = this->channel_groups().first_index_try(fcurve.grp);
      this->channel_group_remove_raw(group_index);
    }
    this->restore_channel_group_invariants();
  }

  array_shift_range(this->fcurve_array,
                    this->fcurve_array_num,
                    fcurve_index,
                    fcurve_index + 1,
                    to_group.fcurve_range_start + to_group.fcurve_range_length);
  to_group.fcurve_range_length++;

  this->restore_channel_group_invariants();

  return true;
}

bool ChannelBag::fcurve_ungroup(FCurve &fcurve)
{
  const int fcurve_index = this->fcurves().first_index_try(&fcurve);
  if (fcurve_index == -1) {
    return false;
  }

  if (fcurve.grp == nullptr) {
    return true;
  }

  bActionGroup *old_group = fcurve.grp;

  array_shift_range(this->fcurve_array,
                    this->fcurve_array_num,
                    fcurve_index,
                    fcurve_index + 1,
                    this->fcurve_array_num - 1);

  old_group->fcurve_range_length--;
  if (old_group->fcurve_range_length == 0) {
    const int old_group_index = this->channel_groups().first_index_try(old_group);
    this->channel_group_remove_raw(old_group_index);
  }

  this->restore_channel_group_invariants();

  return true;
}

ID *action_slot_get_id_for_keying(Main &bmain,
                                  Action &action,
                                  const slot_handle_t slot_handle,
                                  ID *primary_id)
{
  if (animrig::legacy::action_treat_as_legacy(action)) {
    if (primary_id && get_action(*primary_id) == &action) {
      return primary_id;
    }
    return nullptr;
  }

  Slot *slot = action.slot_for_handle(slot_handle);
  if (slot == nullptr) {
    return nullptr;
  }

  blender::Span<ID *> users = slot->users(bmain);
  if (users.size() == 1) {
    /* We only do this for `users.size() == 1` and not `users.size() >= 1`
     * because when there's more than one user it's ambiguous which user we
     * should return, and that would be unpredictable for end users of Blender.
     * We also expect that to be a corner case anyway.  So instead we let that
     * case either get disambiguated by the primary ID in the case below, or
     * return null. */
    return users[0];
  }
  if (users.contains(primary_id)) {
    return primary_id;
  }

  return nullptr;
}

ID *action_slot_get_id_best_guess(Main &bmain, Slot &slot, ID *primary_id)
{
  blender::Span<ID *> users = slot.users(bmain);
  if (users.is_empty()) {
    return nullptr;
  }
  if (users.contains(primary_id)) {
    return primary_id;
  }
  return users[0];
}

slot_handle_t first_slot_handle(const ::bAction &dna_action)
{
  const Action &action = dna_action.wrap();
  if (action.slot_array_num == 0) {
    return Slot::unassigned;
  }
  return action.slot_array[0]->handle;
}

void assert_baklava_phase_1_invariants(const Action &action)
{
  if (action.is_action_legacy()) {
    return;
  }
  if (action.layers().is_empty()) {
    return;
  }
  BLI_assert(action.layers().size() == 1);

  assert_baklava_phase_1_invariants(*action.layer(0));
}

void assert_baklava_phase_1_invariants(const Layer &layer)
{
  if (layer.strips().is_empty()) {
    return;
  }
  BLI_assert(layer.strips().size() == 1);

  assert_baklava_phase_1_invariants(*layer.strip(0));
}

void assert_baklava_phase_1_invariants(const Strip &strip)
{
  UNUSED_VARS_NDEBUG(strip);
  BLI_assert(strip.type() == Strip::Type::Keyframe);
  BLI_assert(strip.is_infinite());
  BLI_assert(strip.frame_offset == 0.0);
}

Action *convert_to_layered_action(Main &bmain, const Action &legacy_action)
{
  if (!legacy_action.is_action_legacy()) {
    return nullptr;
  }

  std::string suffix = "_layered";
  /* In case the legacy action has a long name it is shortened to make space for the suffix. */
  char legacy_name[MAX_ID_NAME - 10];
  /* Offsetting the id.name to remove the ID prefix (AC) which gets added back later. */
  STRNCPY_UTF8(legacy_name, legacy_action.id.name + 2);

  const std::string layered_action_name = std::string(legacy_name) + suffix;
  bAction *dna_action = BKE_action_add(&bmain, layered_action_name.c_str());

  Action &converted_action = dna_action->wrap();
  Slot &slot = converted_action.slot_add();
  Layer &layer = converted_action.layer_add(legacy_action.id.name);
  Strip &strip = layer.strip_add(converted_action, Strip::Type::Keyframe);
  BLI_assert(strip.data<StripKeyframeData>(converted_action).channelbag_array_num == 0);
  ChannelBag *bag = &strip.data<StripKeyframeData>(converted_action).channelbag_for_slot_add(slot);

  const int fcu_count = BLI_listbase_count(&legacy_action.curves);
  bag->fcurve_array = MEM_cnew_array<FCurve *>(fcu_count, "Convert to layered action");
  bag->fcurve_array_num = fcu_count;

  int i = 0;
  blender::Map<FCurve *, FCurve *> old_new_fcurve_map;
  LISTBASE_FOREACH_INDEX (FCurve *, fcu, &legacy_action.curves, i) {
    bag->fcurve_array[i] = BKE_fcurve_copy(fcu);
    bag->fcurve_array[i]->grp = nullptr;
    old_new_fcurve_map.add(fcu, bag->fcurve_array[i]);
  }

  LISTBASE_FOREACH (bActionGroup *, group, &legacy_action.groups) {
    /* The resulting group might not have the same name, because the legacy system allowed
     * duplicate names while the new system ensures uniqueness. */
    bActionGroup &converted_group = bag->channel_group_create(group->name);
    LISTBASE_FOREACH (FCurve *, fcu, &group->channels) {
      if (fcu->grp != group) {
        /* Since the group listbase points to the action listbase, it won't stop iterating when
         * reaching the end of the group but iterate to the end of the action FCurves. */
        break;
      }
      FCurve *new_fcurve = old_new_fcurve_map.lookup(fcu);
      bag->fcurve_assign_to_channel_group(*new_fcurve, converted_group);
    }
  }

  return &converted_action;
}

/**
 * Clone information from the given slot into this slot while retaining important info like the
 * slot handle and runtime data. This copies the name which might clash with other names on the
 * action. Call `slot_name_ensure_unique` after.
 */
static void clone_slot(Slot &from, Slot &to)
{
  ActionSlotRuntimeHandle *runtime = to.runtime;
  slot_handle_t handle = to.handle;
  *reinterpret_cast<ActionSlot *>(&to) = *reinterpret_cast<ActionSlot *>(&from);
  to.runtime = runtime;
  to.handle = handle;
}

void move_slot(Main &bmain, Slot &source_slot, Action &from_action, Action &to_action)
{
  BLI_assert(from_action.slots().contains(&source_slot));
  BLI_assert(&from_action != &to_action);

  /* No merging of strips or layers is handled. All data is put into the assumed single strip. */
  assert_baklava_phase_1_invariants(from_action);
  assert_baklava_phase_1_invariants(to_action);

  StripKeyframeData &from_strip_data = from_action.layer(0)->strip(0)->data<StripKeyframeData>(
      from_action);
  StripKeyframeData &to_strip_data = to_action.layer(0)->strip(0)->data<StripKeyframeData>(
      to_action);

  Slot &target_slot = to_action.slot_add();
  clone_slot(source_slot, target_slot);
  slot_name_ensure_unique(to_action, target_slot);

  ChannelBag *channel_bag = from_strip_data.channelbag_for_slot(source_slot.handle);
  BLI_assert(channel_bag != nullptr);
  channel_bag->slot_handle = target_slot.handle;
  grow_array_and_append<ActionChannelBag *>(
      &to_strip_data.channelbag_array, &to_strip_data.channelbag_array_num, channel_bag);
  int index = from_strip_data.find_channelbag_index(*channel_bag);
  shrink_array_and_remove<ActionChannelBag *>(
      &from_strip_data.channelbag_array, &from_strip_data.channelbag_array_num, index);

  /* Reassign all users of `source_slot` to the action `to_action` and the slot `target_slot`. */
  for (ID *user : source_slot.users(bmain)) {
    const auto assign_other_action =
        [&](bAction *&action_ptr_ref, slot_handle_t &slot_handle_ref, char *slot_name) -> bool {
      /* Only reassign if the reference is actually from the same action. Could be from a different
       * action when using the NLA or action constraints. */
      if (action_ptr_ref != &from_action) {
        return true;
      }

      { /* Assign the Action. */
        const bool assign_ok = generic_assign_action(
            *user, &to_action, action_ptr_ref, slot_handle_ref, slot_name);
        BLI_assert_msg(assign_ok, "Expecting slotted Actions to always be assignable");
        UNUSED_VARS_NDEBUG(assign_ok);
      }
      { /* Assign the Slot. */
        const ActionSlotAssignmentResult result = generic_assign_action_slot(
            &target_slot, *user, action_ptr_ref, slot_handle_ref, slot_name);
        BLI_assert(result == ActionSlotAssignmentResult::OK);
        UNUSED_VARS_NDEBUG(result);
      }
      return true;
    };
    foreach_action_slot_use_with_references(*user, assign_other_action);
  }

  from_action.slot_remove(source_slot);
}

}  // namespace blender::animrig