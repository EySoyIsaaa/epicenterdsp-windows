// Epicenter Drop — process a song with the Epicenter "fuerte" bass effect.
// Two ways to use it:
//   1) Drag one or more songs ONTO this .exe.
//   2) Double-click it -> a file picker opens so you can choose a song.
// Output: "<song>_EPICENTER.mp3" in an "output" folder next to the song.
// Robust: finds ffmpeg by absolute path, never closes silently, logs to %TEMP%\epicenter_drop.log.
#include "BassGlueV2.hpp"
#include "wav.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

namespace fs = std::filesystem;
static std::ofstream g_log;

static void logln(const std::string& s) {
    std::printf("%s\n", s.c_str());
    if (g_log) { g_log << s << "\n"; g_log.flush(); }
}
static std::string dirOf(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string(".") : p.substr(0, s);
}
static std::string baseOf(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    std::string name = s == std::string::npos ? p : p.substr(s + 1);
    size_t d = name.find_last_of('.');
    return d == std::string::npos ? name : name.substr(0, d);
}

static std::string findFfmpeg() {
    std::error_code ec;
    if (const char* la = std::getenv("LOCALAPPDATA")) {
        fs::path root = fs::path(la) / "Microsoft" / "WinGet";
        fs::path shim = root / "Links" / "ffmpeg.exe";
        if (fs::exists(shim, ec)) return shim.string();
        fs::path pkgs = root / "Packages";
        if (fs::exists(pkgs, ec)) {
            for (auto& d : fs::directory_iterator(pkgs, ec)) {
                std::string n = d.path().filename().string();
                if (n.find("FFmpeg") != std::string::npos || n.find("ffmpeg") != std::string::npos)
                    for (auto& f : fs::recursive_directory_iterator(d.path(), ec))
                        if (f.path().filename() == "ffmpeg.exe") return f.path().string();
            }
        }
    }
    return "ffmpeg";
}

static int runCmd(const std::string& inner) {
    return std::system(("\"" + inner + "\"").c_str());   // outer quotes for cmd
}
static void pause() { std::system("pause"); }

// File picker (used when the exe is double-clicked with no file dropped).
static std::string pickFile() {
#ifdef _WIN32
    char file[2048] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Audio (flac, mp3, wav, m4a)\0*.flac;*.mp3;*.wav;*.m4a;*.ogg;*.opus;*.aac\0Todos los archivos\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrTitle = "Elige una cancion para procesar con Epicenter";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) return std::string(file);
#endif
    return "";
}

static bool processOne(const std::string& input, const std::string& ff) {
    try {
        logln("\n----------------------------------------");
        logln("Entrada: " + input);
        const std::string dir = dirOf(input);
        const std::string base = baseOf(input);
        const std::string outDir = dir + "/output";
        std::error_code ec;
        fs::create_directories(outDir, ec);
        const std::string tmpIn = outDir + "/_epi_in.wav";
        const std::string tmpOut = outDir + "/_epi_out.wav";
        const std::string outMp3 = outDir + "/" + base + "_EPICENTER.mp3";

        logln("[1/3] decodificando...");
        int rc = runCmd("\"" + ff + "\" -y -hide_banner -loglevel error -i \"" + input + "\" -ac 2 -ar 44100 -c:a pcm_s16le \"" + tmpIn + "\"");
        if (rc != 0 || !fs::exists(tmpIn, ec)) { logln("ERROR: no pude decodificar (rc=" + std::to_string(rc) + ")."); return false; }

        wav::Audio a;
        if (!wav::read(tmpIn, a) || a.frames() == 0) { logln("ERROR: WAV temporal invalido."); return false; }
        logln("[2/3] aplicando Epicenter (fuerte)...");
        lab::BassGlueV2 g;
        g.prepare(a.sampleRate, a.channels);
        lab::BassGlueV2Params p;                 // "fuerte" preset
        p.subBoostDb = 18.0f; p.subGen = 2.4f; p.subDepth = 0.9f; p.monoHz = 105.0f; p.scoopDb = 10.0f;
        g.setParameters(p);
        float* ptrs[2] = { a.ch[0].data(), a.channels > 1 ? a.ch[1].data() : a.ch[0].data() };
        g.process(ptrs, a.channels, a.frames());
        if (!wav::write16(tmpOut, a)) { logln("ERROR: no pude escribir WAV procesado."); return false; }

        logln("[3/3] codificando mp3...");
        runCmd("\"" + ff + "\" -y -hide_banner -loglevel error -i \"" + tmpOut + "\" -af loudnorm=I=-13:TP=-1.5:LRA=11 -b:a 320k \"" + outMp3 + "\"");
        std::remove(tmpIn.c_str()); std::remove(tmpOut.c_str());
        logln("LISTO -> " + outMp3);
        return true;
    } catch (const std::exception& e) { logln(std::string("EXCEPCION: ") + e.what()); }
    catch (...) { logln("EXCEPCION desconocida."); }
    return false;
}

int main(int argc, char** argv) {
    if (const char* tmp = std::getenv("TEMP"))
        g_log.open(std::string(tmp) + "/epicenter_drop.log");
    std::printf("\n=== EpicenterDSP - procesador de graves ===\n");

    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) files.push_back(argv[i]);
    if (files.empty()) {                          // double-clicked: open a file picker
        std::printf("\nElige una cancion en la ventana que se abrio...\n");
        std::string f = pickFile();
        if (!f.empty()) files.push_back(f);
    }
    if (files.empty()) {
        logln("\nNo se eligio ningun archivo. Arrastra una cancion sobre el .exe o elige una al abrirlo.");
        pause();
        return 0;
    }

    const std::string ff = findFfmpeg();
    logln("ffmpeg: " + ff);
    int ok = 0;
    for (const auto& f : files) if (processOne(f, ff)) ++ok;
    logln("\n=== " + std::to_string(ok) + "/" + std::to_string(files.size()) + " procesadas. Revisa la carpeta 'output'. ===");
    pause();
    return 0;
}
