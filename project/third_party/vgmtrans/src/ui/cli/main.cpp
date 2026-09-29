#include "pch.h"
#include "CLIVGMRoot.h"
#include "VGMColl.h"
#include "SF2File.h"
#include "DLSFile.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSeq.h"
#include "Root.h"
#include <iostream>
#include <string>
#include <vector>
#include <set>

#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

static std::string getFileStem(const std::string &path)
{
    size_t lastSlash = path.find_last_of("/\\");
    std::string filename = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
    size_t lastDot = filename.find_last_of('.');
    return (lastDot == std::string::npos) ? filename : filename.substr(0, lastDot);
}

static std::string narrow(const std::wstring &w)
{
    return std::string(w.begin(), w.end());
}

static std::string safeName(std::string n)
{
    for (auto &c : n)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<'
            || c == '>' || c == '|')
            c = '_';
    return n.empty() ? "unnamed" : n;
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_dir> [--sf2] [--dls]\n";
        return 1;
    }

    std::string inFile = argv[1];
    std::string outDir = argv[2];
    bool exportSF2 = true;
    bool exportDLS = false;
    bool mergedSF2 = false;

    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--dls") {
            exportDLS = true;
        } else if (arg == "--sf2") {
            exportSF2 = true;
        } else if (arg == "--merged") {
            mergedSF2 = true;
        }
    }

#ifdef _WIN32
    mkdir(outDir.c_str());
#else
    mkdir(outDir.c_str(), 0755);
#endif

    CLIVGMRoot cliRoot;
    std::string stem = getFileStem(inFile);

    cliRoot.UI_SetRootPtr(&pRoot);
    cliRoot.Init();
    cliRoot.saveDirPath = std::wstring(outDir.begin(), outDir.end());

    std::wstring wInFile(inFile.begin(), inFile.end());
    std::wstring wStem(stem.begin(), stem.end());

    if (!cliRoot.OpenRawFile(wInFile)) {
        std::cerr << "vgmtrans: failed to open file: " << inFile << "\n";
        cliRoot.Exit();
        return 1;
    }

    if (cliRoot.vVGMColl.empty()) {
        std::cerr << "vgmtrans: no collections found in file.\n";
        cliRoot.Exit();
        return 1;
    }

    // MIDI goes to <out>/midi, soundfonts to <out>/soundfonts; sequences.tsv
    // records which soundfont (bank) each MIDI must be played with.
    std::string midiDir = outDir + "/midi";
    std::string sfDir = outDir + "/soundfonts";
#ifdef _WIN32
    mkdir(midiDir.c_str());
    mkdir(sfDir.c_str());
#else
    mkdir(midiDir.c_str(), 0755);
    mkdir(sfDir.c_str(), 0755);
#endif
    std::set<std::string> usedMidi;
    std::string manifest = "midi\tsoundfont\n";
    for (size_t i = 0; i < cliRoot.vVGMColl.size(); ++i) {
        VGMColl *coll = cliRoot.vVGMColl[i];
        if (coll && coll->seq) {
            std::string name = safeName(narrow(*coll->GetName()));
            std::string unique = name;
            for (int n = 2; !usedMidi.insert(unique).second; n++)
                unique = name + "_" + std::to_string(n);
            std::string bank;
            if (!coll->instrsets.empty() && coll->instrsets[0])
                bank = safeName(narrow(*coll->instrsets[0]->GetName()));
            std::string path = midiDir + "/" + unique + ".mid";
            coll->seq->SaveAsMidi(std::wstring(path.begin(), path.end()));
            manifest += unique + ".mid\t" + (bank.empty() ? "" : bank + ".sf2") + "\n";
        }
    }
    {
        std::string mp = midiDir + "/sequences.tsv";
        FILE *mf = fopen(mp.c_str(), "wb");
        if (mf) {
            fwrite(manifest.data(), 1, manifest.size(), mf);
            fclose(mf);
        }
    }

    // Collect all unique instrument sets and sample collections across the whole archive
    std::vector<VGMInstrSet *> allInstrSets;
    std::vector<VGMSampColl *> allSampColls;
    std::set<VGMInstrSet *> seenInstrSets;
    std::set<VGMSampColl *> seenSampColls;

    for (size_t i = 0; i < cliRoot.vVGMColl.size(); ++i) {
        VGMColl *coll = cliRoot.vVGMColl[i];
        if (!coll) continue;

        for (size_t j = 0; j < coll->instrsets.size(); ++j) {
            VGMInstrSet *is = coll->instrsets[j];
            if (is && seenInstrSets.insert(is).second)
                allInstrSets.push_back(is);
        }

        for (size_t j = 0; j < coll->sampcolls.size(); ++j) {
            VGMSampColl *sc = coll->sampcolls[j];
            if (sc && seenSampColls.insert(sc).second)
                allSampColls.push_back(sc);
        }
    }

    // If collections didn't attach instrsets/sampcolls, check loaded VGMFiles in root
    if (allInstrSets.empty() || allSampColls.empty()) {
        for (size_t i = 0; i < cliRoot.vVGMFile.size(); ++i) {
            VGMFile *f = cliRoot.vVGMFile[i];
            if (!f) continue;
            if (f->GetFileType() == FILETYPE_INSTRSET) {
                VGMInstrSet *is = (VGMInstrSet *)f;
                if (seenInstrSets.insert(is).second)
                    allInstrSets.push_back(is);
            } else if (f->GetFileType() == FILETYPE_SAMPCOLL) {
                VGMSampColl *sc = (VGMSampColl *)f;
                if (seenSampColls.insert(sc).second)
                    allSampColls.push_back(sc);
            }
        }
    }

    // One SoundFont per bank (instrument set + the sample collections of the
    // collection(s) using it), the way vgmtrans' own GUI exports them. Each
    // MIDI's program numbers are only valid against its own bank, so a merged
    // font (--merged) can pick wrong instruments when banks collide.
    if (!mergedSF2 && (exportSF2 || exportDLS)) {
        std::set<VGMInstrSet *> done;
        std::set<std::string> usedSf;
        for (size_t i = 0; i < cliRoot.vVGMColl.size(); ++i) {
            VGMColl *coll = cliRoot.vVGMColl[i];
            if (!coll) continue;
            for (size_t j = 0; j < coll->instrsets.size(); ++j) {
                VGMInstrSet *is = coll->instrsets[j];
                if (!is || !done.insert(is).second) continue;
                std::string name = safeName(narrow(*is->GetName()));
                std::string unique = name;
                for (int n = 2; !usedSf.insert(unique).second; n++)
                    unique = name + "_" + std::to_string(n);
                std::wstring wName(unique.begin(), unique.end());
                VGMColl bankColl(wName);
                bankColl.AddInstrSet(is);
                for (size_t k = 0; k < coll->sampcolls.size(); ++k)
                    bankColl.AddSampColl(coll->sampcolls[k]);
                std::wstring base = std::wstring(sfDir.begin(), sfDir.end()) + L"/" + wName;
                if (exportSF2) {
                    SF2File *sf2file = bankColl.CreateSF2File();
                    if (sf2file != NULL) {
                        sf2file->SaveSF2File(base + L".sf2");
                        delete sf2file;
                    }
                }
                if (exportDLS) {
                    DLSFile dlsfile;
                    if (bankColl.CreateDLSFile(dlsfile))
                        dlsfile.SaveDLSFile(base + L".dls");
                }
                bankColl.RemoveFileAssocs();
            }
        }
        cliRoot.Exit();
        return 0;
    }

    // Create 1 combined master SoundFont for the entire archive
    if (!allInstrSets.empty() && !allSampColls.empty()) {
        VGMColl masterColl(wStem);
        for (size_t i = 0; i < allInstrSets.size(); ++i)
            masterColl.AddInstrSet(allInstrSets[i]);
        for (size_t i = 0; i < allSampColls.size(); ++i)
            masterColl.AddSampColl(allSampColls[i]);

        if (exportSF2) {
            SF2File *sf2file = masterColl.CreateSF2File();
            if (sf2file != NULL) {
                std::wstring sf2filepath = cliRoot.saveDirPath + L"/" + wStem + L".sf2";
                sf2file->SaveSF2File(sf2filepath);
                delete sf2file;
            }
        }

        if (exportDLS) {
            DLSFile dlsfile;
            if (masterColl.CreateDLSFile(dlsfile)) {
                std::wstring dlsfilepath = cliRoot.saveDirPath + L"/" + wStem + L".dls";
                dlsfile.SaveDLSFile(dlsfilepath);
            }
        }
        masterColl.RemoveFileAssocs();
    } else if (!cliRoot.vVGMColl.empty()) {
        VGMColl *coll = cliRoot.vVGMColl[0];
        if (exportSF2) {
            SF2File *sf2file = coll->CreateSF2File();
            if (sf2file != NULL) {
                std::wstring sf2filepath = cliRoot.saveDirPath + L"/" + wStem + L".sf2";
                sf2file->SaveSF2File(sf2filepath);
                delete sf2file;
            }
        }
        if (exportDLS) {
            DLSFile dlsfile;
            if (coll->CreateDLSFile(dlsfile)) {
                std::wstring dlsfilepath = cliRoot.saveDirPath + L"/" + wStem + L".dls";
                dlsfile.SaveDLSFile(dlsfilepath);
            }
        }
    }

    cliRoot.Exit();
    return 0;
}
