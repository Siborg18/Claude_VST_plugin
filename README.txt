HARDKICK FORGE  -  Hardstyle kick distortion (64-bit Windows)
=============================================================

INSTALL (FL Studio)
1. Copy the whole "HardKickForge.vst3" folder to:
      C:\Program Files\Common Files\VST3\
2. In FL Studio: Options > Manage plugins > Find more plugins (or "Start scan").
3. HardKickForge shows up under Effects (made by "Simon").

USE
1. Put your kick sample in a Channel (Sampler) or on the Playlist as audio.
2. Route that channel to a Mixer insert and load HardKickForge on that insert.
3. Play the kick and turn the knobs. Presets are in the plugin's preset menu (top-left arrow in FL's wrapper).
   Tip: set the kick length/decay with the Sampler's volume envelope - the plugin sustains the tail.

CONTROLS
 DISTORTION
  Drive      - total gain into the clippers (0-60 dB)
  Stages     - 1-4 clippers in series (more = denser, more "raw")
  Clip Mode  - Soft (tanh), Hard (square), Fold (screechy wavefolder), Asym (even harmonics, crunchy)
 TONE (PRE-EQ, applied before each clip stage - this is what shapes the hardstyle "punch/screech")
  Tone Freq  - where the boost sits (100 Hz - 5 kHz). ~700-1300 Hz is classic.
  Tone Gain  - boost/cut in dB
  Tone Q     - width of the boost
 TAIL
  Tail       - amount of short reverb fed INTO the distortion (thicker, longer tail)
  Tail Size  - length/brightness of that reverb
 OUTPUT
  Low Cut / High Cut - clean up rumble and fizz after distortion
  Mix        - blend with the clean kick
  Output     - output level (drives the final clipper when it is ON)
  FINAL CLIPPER button - hard ceiling at 0 dBFS so the kick is loud but never goes over

 Mouse: drag up/down, Shift+drag = fine, double-click = reset, mouse wheel works too.

Processing is 4x oversampled (less aliasing); latency 24 samples, compensated automatically by FL.

OTHER FORMATS
 "Other formats" contains a CLAP version (FL Studio 2024+ supports CLAP -> C:\Program Files\Common Files\CLAP\)
 and a VST2 .dll. You only need one of the three; VST3 is recommended.

SOURCE
 Built with DPF (DISTRHO Plugin Framework). To rebuild: clone https://github.com/DISTRHO/DPF next to
 the Source folder and run "make" (MinGW-w64 on Windows or Linux cross-compile).
