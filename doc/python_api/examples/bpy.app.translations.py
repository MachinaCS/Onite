"""
Introduction
------------

.. warning::

   Most of this object should only be useful if you actually manipulate i18n stuff from Python.
   If you are a regular add-on, you should only bother about :const:`contexts` member,
   and the :func:`register`/:func:`unregister` functions! The :func:`pgettext` family of functions
   should only be used in rare, specific cases (like e.g. complex "composited" UI strings...).

To add translations to your python script, you must define a dictionary formatted like that:
``{locale: {msg_key: msg_translation, ...}, ...}`` where:

- locale is either a lang iso code (e.g. ``fr``), a lang+country code (e.g. ``pt_BR``),
  a lang+variant code (e.g. ``sr@latin``), or a full code (e.g. ``uz_UZ@cyrilic``).
- msg_key is a tuple (context, org message) - use, as much as possible, the predefined :const:`contexts`.
- msg_translation is the translated message in given language!

Then, call ``bpy.app.translations.register(__name__, your_dict)`` in your ``register()`` function, and
``bpy.app.translations.unregister(__name__)`` in your ``unregister()`` one.

The ``Manage UI translations`` add-on has several functions to help you collect strings to translate, and
generate the needed python code (the translation dictionary), as well as optional intermediary po files
if you want some... See
`How to Translate Blender <https://developer.blender.org/docs/handbook/translating/translator_guide/>`_ and
`Using i18n in Blender Code <https://developer.blender.org/docs/handbook/translating/developer_guide/>`_
for more info.

Module References
-----------------

"""

import bpy

# This block can be automatically generated by UI translations addon, which also handles conversion with PO format.
# See also https://developer.blender.org/docs/handbook/translating/translator_guide/#translating-non-official-add-ons
# It can (should) also be put in a different, specific py file.

# ##### BEGIN AUTOGENERATED I18N SECTION #####
# NOTE: You can safely move around this auto-generated block (with the begin/end markers!),
#       and edit the translations by hand.
#       Just carefully respect the format of the tuple!

# Tuple of tuples ((msgctxt, msgid), (sources, gen_comments), (lang, translation, (is_fuzzy, comments)), ...)
translations_tuple = (
    (("*", ""),
     ((), ()),
     ("fr_FR", "Project-Id-Version: Copy Settings 0.1.5 (r0)\nReport-Msgid-Bugs-To: \nPOT-Creation-Date: 2013-04-18 15:27:45.563524\nPO-Revision-Date: 2013-04-18 15:38+0100\nLast-Translator: Bastien Montagne <montagne29@wanadoo.fr>\nLanguage-Team: LANGUAGE <LL@li.org>\nLanguage: __POT__\nMIME-Version: 1.0\nContent-Type: text/plain; charset=UTF-8\nContent-Transfer-Encoding: 8bit\n",
               (False,
                ("Blender's translation file (po format).",
                 "Copyright (C) 2013 The Blender Foundation.",
                 "This file is distributed under the same license as the Blender package.",
                 "FIRST AUTHOR <EMAIL@ADDRESS>, YEAR."))),
     ),
    (("Operator", "Render: Copy Settings"),
     (("bpy.types.SCENE_OT_render_copy_settings",),
      ()),
     ("fr_FR", "Rendu : copier réglages",
               (False, ())),
     ),
    (("*", "Copy render settings from current scene to others"),
     (("bpy.types.SCENE_OT_render_copy_settings",),
      ()),
     ("fr_FR", "Copier les réglages de rendu depuis la scène courante vers d’autres",
               (False, ())),
     ),
    # ... etc, all messages from your addon.
)

translations_dict = {}
for msg in translations_tuple:
    key = msg[0]
    for lang, trans, (is_fuzzy, comments) in msg[2:]:
        if trans and not is_fuzzy:
            translations_dict.setdefault(lang, {})[key] = trans

# ##### END AUTOGENERATED I18N SECTION #####

# Define remaining addon (operators, UI...) here.


def register():
   # Usual operator/UI/etc. registration...

    bpy.app.translations.register(__name__, translations_dict)


def unregister():
    bpy.app.translations.unregister(__name__)

   # Usual operator/UI/etc. unregistration...
