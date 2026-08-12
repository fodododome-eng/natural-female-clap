# Natural Female Formant — MicUp CLAP prototype

This is a **prototype** CLAP audio effect intended for MicUp 1.0.6 on Android ARM64.

It does **formant-envelope shifting**, not AI voice cloning. The starting formant factor is 1.18 and pitch is intentionally left alone so MicUp's built-in Pitch Shifter can be used separately.

## Important

This project has **not been tested on the user's Huawei Y8p**. It must be built for Android `arm64-v8a` before it can be copied to MicUp.

The MicUp host currently processes CLAP plugins as a **mono, in-place buffer**, which this plugin targets.

## Build with GitHub Actions

1. Create a new GitHub repository.
2. Upload this project.
3. Open **Actions**.
4. Run **Build Android CLAP**.
5. Download the generated artifact.
6. Copy `natural_female.clap` to the phone.
7. Open it with MicUp, or add its folder under Settings → Manage Plugin Paths.
8. Rescan Plugin Directories.
9. Add **Natural Female Formant** to the plugin chain.

## Suggested MicUp settings

Start with:
- MicUp Pitch Shifter: **+2.5 to +3.5 st**
- Natural Female Formant: default
- Reverb: OFF
- EQ: flat initially
- Monitor: ON with wired headphones

If the voice is still too masculine, the plugin source can be changed from `FORMANT = 1.18f` to `1.22f` or `1.25f`.

If it sounds metallic or phasey, reduce the formant factor.

## Build locally

Requirements:
- Android Studio / Android NDK 26
- CMake 3.22+
- Android ABI: `arm64-v8a`

The build fetches CLAP 1.2.2 from the official repository.

Official references:
- MicUp: https://github.com/papergray/MicUp
- CLAP: https://github.com/free-audio/clap
