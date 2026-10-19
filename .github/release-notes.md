*a breath, a body, and the sky it dissolves into*

First public release of **Kings into Clowns**, a resonator-and-cloud instrument by mtyas.
Read the [manual](https://github.com/mtyas/Kings-into-Clowns/blob/main/docs/MANUAL.md).

### Downloads

| File | Contains |
|---|---|
| `...-windows-x64.zip` | CLAP, VST3, Standalone (64-bit, needs a CPU with AVX2) |
| `...-macos-universal.zip` | CLAP, VST3, AU, Standalone (Apple Silicon & Intel, macOS 11+) |
| `...-linux-x64.zip` | CLAP, VST3, Standalone |

### Installing

- **Windows** — copy `Kings into Clowns.clap` to `C:\Program Files\Common Files\CLAP\` and the
  `Kings into Clowns.vst3` folder to `C:\Program Files\Common Files\VST3\`.
- **macOS** — copy the `.clap` to `~/Library/Audio/Plug-Ins/CLAP/`, the `.vst3` to
  `~/Library/Audio/Plug-Ins/VST3/` and the `.component` to `~/Library/Audio/Plug-Ins/Components/`.
  These builds are not notarized: if macOS refuses to open them, run
  `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/*/Kings\ into\ Clowns.*` once.
- **Linux** — copy the `.clap` to `~/.clap/` and the `.vst3` folder to `~/.vst3/`.

Then rescan plugins in your DAW.

### Notes

This is a 0.x release: the sound and the patch format may still change. Please report anything
odd in the [issues](https://github.com/mtyas/Kings-into-Clowns/issues).
