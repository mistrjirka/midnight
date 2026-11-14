# Midnight architecture review

Scope: nine Canon cameras, C component boundaries, build scripts and target selection.

## Refactors

- **One camera registry:** modules/astro/camera_profiles.def holds model,
  firmware, sensor width, special focus key and exported symbol filename.
  Firmware uses it as an immutable C table; scripts/cameras.py validates the
  same rows for builds and ZIP packaging. The 1100D's unusual symbol filename
  no longer needs a separate case.
- **Detection once:** Module startup selects a supported firmware and caches
  camera capabilities. Field-of-view and focus code use the selected profile,
  not duplicate multi-camera condition chains. Unsupported firmware fails closed.
- **Focus event handling:** The camera layer validates physical input. A
  separate pure policy decides whether to focus or show the star-size readout.
  All other buttons and touch events pass through to Canon.
- **Menu boundaries:** menu.c is only the declarative layout (about 260 lines).
  menu_handlers.c contains actions and value formatting (about 290 lines).
  Previously a single 516-line file owned both.
- **Test simplification:** Three repeated camera test harnesses became one
  test compiling the actual production camera registry, startup, and raw-key
  handling. Existing tests still compile Magic Lantern's actual key translator.
- **Optional guidance:** Fresh installations start with no target. Selecting
  a catalogue and object enables guidance. Choosing None clears the cached
  object without reading the catalogue. Previously saved targets remain saved.

## Oversized files

- core/sky_verify.c (about 525 lines) owns one numerically sensitive
  verification algorithm with small individual steps, not unrelated services.
  Do not split its numerical operations merely to decrease line count.
- core/scene_layers.c (about 330 lines) builds related sky layers through the
  shared scene context. Its role is cohesive.
- No other code file exceeds 400 lines after the menu split.

## Verification scope

Native tests, solver regressions, firmware compilation, symbol checks and ZIP
checks validate software behavior. They do not replace physical-camera
tests. Previous public releases and existing SD-card installations remain
unchanged until a separately validated release is requested.
