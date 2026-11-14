# New Canon EOS camera ports

Midnight supports the following experimental Magic Lantern cameras. **Their
actual Live View and gallery behavior has not been tested on these bodies.**

| Camera | Canon firmware | Sensor | Gallery controls |
| --- | --- | --- | --- |
| **650D** | **1.0.4** | 22.3 mm wide | Play → Q → SET |
| **100D** | **1.0.1** | 22.3 mm wide | Play → **Av** → SET |
| **60D** | **1.1.1** | 22.3 mm wide | Play → Q → SET |
| **1100D** | **1.0.5** | 22.0 mm wide | Play → Q → SET |
| **70D** | **1.1.2** | 22.5 mm wide | Play → Q → SET |

**100D:** its Q/SET button is shared. Magic Lantern uses the **Av /
exposure-compensation** button as the separate Q action in its menus.
The same action is used for Midnight's gallery shortcut. Test it on a
physical 100D before relying on it.

### Shortcuts and safety

- Midnight accepts photo Live View at **1×**. If zoomed, it asks you to
  return to 1×. Manual star-size focus measurement can use 5× or 10×.
- The special-button autofocus shortcut is **Off** by default on all five
  new models. The existing DISP / LCD-light shortcut only applies to
  **600D** and **6D**. INFO and double half-press can be enabled manually.
- Touchscreen events on the 650D, 100D and 70D are left to Canon.
- The 100D and 70D use Magic Lantern's AllocateMemory region. The 1100D
  also has constrained memory. Midnight may need over 600 KiB of runtime
  allocations during solving. Successful compilation does not guarantee
  those cameras have enough free RAM. Memory failures should produce a
  message instead of proceeding with an invalid pointer.
- The 1100D and 70D have different sensor widths. These are stored in the
  camera-specific field-of-view calculations; they do not require a
  different sky catalogue.

### Install and test

1. Verify the exact Canon firmware shown in the table.
2. Back up the complete SD card, especially photos and `autoexec.bin`.
3. If Magic Lantern already works, extract only the **module-only** ZIP
   for the correct camera. Do not replace existing Magic Lantern boot files.
4. Enable the astro module and restart. Open **Focus → Midnight**.
5. Run **Developer → Solve image file** with `TEST.PGM`.
6. Show a full-screen sky photo and use the listed gallery buttons.
7. Run **Solve Live View** in **1× photo Live View**.
8. Check the result labels and available memory. Keep electronic autofocus
   and unattended mount movement disabled until physical validation.

Firmware must **not** be interchanged between camera models, even if the
Midnight module is built from shared source. A full firmware ZIP is for a
matching model and firmware version on a suitable spare SD card.
