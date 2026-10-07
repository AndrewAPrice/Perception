import os
import sys

def main():
    package_path = os.getcwd()
    source_dir = os.path.join(package_path, "source")
    os.makedirs(source_dir, exist_ok=True)

    # 1. Generate config.h
    print("Generating config.h...")
    config_h_content = """/* Generated for Perception OS */
#ifndef ALSOFT_CONFIG_H
#define ALSOFT_CONFIG_H

#define ALSOFT_EAX 1
#define FORCE_ALIGN
#define HAVE_POSIX_MEMALIGN
#define HAVE_GETOPT
#define HAVE_DLFCN_H
#define HAVE_MALLOC_H
#define HAVE_CPUID_H
#define HAVE_GCC_GET_CPUID
#define HAVE_PTHREAD_SETSCHEDPARAM
#define HAVE_PTHREAD_SETNAME_NP

#define ALSOFT_INSTALL_DATADIR "/system/share/openal"

/* Disabled backends */
#define HAVE_ALSA 0
#define HAVE_OSS 0
#define HAVE_SOLARIS 0
#define HAVE_SNDIO 0
#define HAVE_PORTAUDIO 0
#define HAVE_PULSEAUDIO 0
#define HAVE_COREAUDIO 0
#define HAVE_WASAPI 0
#define HAVE_DSOUND 0
#define HAVE_WINMM 0
#define HAVE_JACK 0
#define HAVE_OPENSL 0
#define HAVE_OBOE 0
#define HAVE_PIPEWIRE 0

#endif
"""
    with open(os.path.join(source_dir, "config.h"), "w") as f:
        f.write(config_h_content)

    # 2. Generate config_backends.h
    print("Generating config_backends.h...")
    config_backends_h = """/* Generated for Perception OS */
#ifndef CONFIG_BACKENDS_H
#define CONFIG_BACKENDS_H

#define HAVE_ALSA 0
#define HAVE_OSS 0
#define HAVE_PIPEWIRE 0
#define HAVE_SOLARIS 0
#define HAVE_SNDIO 0
#define HAVE_WASAPI 0
#define HAVE_DSOUND 0
#define HAVE_WINMM 0
#define HAVE_PORTAUDIO 0
#define HAVE_PULSEAUDIO 0
#define HAVE_JACK 0
#define HAVE_COREAUDIO 0
#define HAVE_OPENSL 0
#define HAVE_OBOE 0
#define HAVE_WAVE 1
#define HAVE_SDL3 0
#define HAVE_SDL2 1

#endif
"""
    with open(os.path.join(source_dir, "config_backends.h"), "w") as f:
        f.write(config_backends_h)

    # 3. Generate config_simd.h
    print("Generating config_simd.h...")
    config_simd_h = """/* Generated for Perception OS */
#ifndef CONFIG_SIMD_H
#define CONFIG_SIMD_H

#define HAVE_SSE 1
#define HAVE_SSE2 1
#define HAVE_SSE3 1
#define HAVE_SSE4_1 1
#define HAVE_SSE_INTRINSICS 1
#define HAVE_NEON 0

#endif
"""
    with open(os.path.join(source_dir, "config_simd.h"), "w") as f:
        f.write(config_simd_h)

    # 4. Generate version.h
    print("Generating version.h...")
    version_h_content = """/* Generated for Perception OS */
#ifndef ALSOFT_VERSION_H
#define ALSOFT_VERSION_H

#define ALSOFT_VERSION "1.23.1"
#define ALSOFT_VERSION_NUM 1,23,1,0
#define ALSOFT_GIT_BRANCH "master"
#define ALSOFT_GIT_COMMIT_HASH "unknown"

#endif
"""
    with open(os.path.join(source_dir, "version.h"), "w") as f:
        f.write(version_h_content)

    print("OpenAL-Soft header generation complete!")

if __name__ == "__main__":
    main()
