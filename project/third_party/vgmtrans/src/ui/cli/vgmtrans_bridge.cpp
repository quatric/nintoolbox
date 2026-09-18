#include "pch.h"
#include "vgmtrans_bridge.h"
#include "CLIVGMRoot.h"
#include "VGMColl.h"
#include "SF2File.h"
#include "DLSFile.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSeq.h"
#include "Root.h"
#include "RSARScanner.h"
#include <iostream>
#include <string>
#include <vector>
#include <set>

static std::string getFileStem(const std::string &path)
{
    size_t lastSlash = path.find_last_of("/\\");
    std::string filename = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
    size_t lastDot = filename.find_last_of('.');
    return (lastDot == std::string::npos) ? filename : filename.substr(0, lastDot);
}

extern "C" int VgmtransConvertFileExt(const char *inFile, const char *outDir, int formatFlags)
{
    CLIVGMRoot cliRoot;

    std::string inFileStr = inFile;
    std::string outDirStr = outDir;
    std::string stem = getFileStem(inFileStr);

    cliRoot.UI_SetRootPtr(&pRoot);
    cliRoot.Init();
    cliRoot.saveDirPath = std::wstring(outDirStr.begin(), outDirStr.end());

    std::wstring wInFile(inFileStr.begin(), inFileStr.end());
    std::wstring wStem(stem.begin(), stem.end());

    // RSAR's WAVE-type (one-shot sample) sounds are exported as standalone
    // .wav files directly during Scan() -- they never produce a VGMColl,
    // since there's no sequence/bank to route through the MIDI+SF2 pipeline
    // below. Reset the counter so a WAVE-only BRSAR isn't mistaken for a
    // failure just because vVGMColl came back empty.
    g_rsarWaveSoundsExported = 0;

    if (!cliRoot.OpenRawFile(wInFile))
    {
        std::cerr << "vgmtrans: failed to open file: " << inFileStr << "\n";
        cliRoot.Exit();
        return 1;
    }

    if (cliRoot.vVGMColl.empty() && g_rsarWaveSoundsExported == 0)
    {
        std::cerr << "vgmtrans: no collections found in file.\n";
        cliRoot.Exit();
        return 1;
    }

    // Export all MIDI sequences
    for (size_t i = 0; i < cliRoot.vVGMColl.size(); ++i)
    {
        VGMColl *coll = cliRoot.vVGMColl[i];
        if (coll && coll->seq)
        {
            std::wstring name = *coll->GetName();
            std::wstring midifilepath = cliRoot.saveDirPath + L"/" + name + L".mid";
            coll->seq->SaveAsMidi(midifilepath);
        }
    }

    // Collect all unique instrument sets and sample collections across the whole archive
    std::vector<VGMInstrSet *> allInstrSets;
    std::vector<VGMSampColl *> allSampColls;
    std::set<VGMInstrSet *> seenInstrSets;
    std::set<VGMSampColl *> seenSampColls;

    for (size_t i = 0; i < cliRoot.vVGMColl.size(); ++i)
    {
        VGMColl *coll = cliRoot.vVGMColl[i];
        if (!coll) continue;

        for (size_t j = 0; j < coll->instrsets.size(); ++j)
        {
            VGMInstrSet *is = coll->instrsets[j];
            if (is && seenInstrSets.insert(is).second)
                allInstrSets.push_back(is);
        }

        for (size_t j = 0; j < coll->sampcolls.size(); ++j)
        {
            VGMSampColl *sc = coll->sampcolls[j];
            if (sc && seenSampColls.insert(sc).second)
                allSampColls.push_back(sc);
        }
    }

    // If collections didn't attach instrsets/sampcolls, check loaded VGMFiles in root
    if (allInstrSets.empty() || allSampColls.empty())
    {
        for (size_t i = 0; i < cliRoot.vVGMFile.size(); ++i)
        {
            VGMFile *f = cliRoot.vVGMFile[i];
            if (!f) continue;
            if (f->GetFileType() == FILETYPE_INSTRSET)
            {
                VGMInstrSet *is = (VGMInstrSet *)f;
                if (seenInstrSets.insert(is).second)
                    allInstrSets.push_back(is);
            }
            else if (f->GetFileType() == FILETYPE_SAMPCOLL)
            {
                VGMSampColl *sc = (VGMSampColl *)f;
                if (seenSampColls.insert(sc).second)
                    allSampColls.push_back(sc);
            }
        }
    }

    // Create 1 combined master SoundFont for the entire archive
    if (!allInstrSets.empty() && !allSampColls.empty())
    {
        VGMColl masterColl(wStem);
        for (size_t i = 0; i < allInstrSets.size(); ++i)
            masterColl.AddInstrSet(allInstrSets[i]);
        for (size_t i = 0; i < allSampColls.size(); ++i)
            masterColl.AddSampColl(allSampColls[i]);

        // Export SF2 (formatFlags & VGMTRANS_FMT_SF2)
        if (formatFlags & VGMTRANS_FMT_SF2)
        {
            SF2File *sf2file = masterColl.CreateSF2File();
            if (sf2file != NULL)
            {
                std::wstring sf2filepath = cliRoot.saveDirPath + L"/" + wStem + L".sf2";
                sf2file->SaveSF2File(sf2filepath);
                delete sf2file;
            }
        }

        // Export DLS (formatFlags & VGMTRANS_FMT_DLS)
        if (formatFlags & VGMTRANS_FMT_DLS)
        {
            DLSFile dlsfile;
            if (masterColl.CreateDLSFile(dlsfile))
            {
                std::wstring dlsfilepath = cliRoot.saveDirPath + L"/" + wStem + L".dls";
                dlsfile.SaveDLSFile(dlsfilepath);
            }
        }
        masterColl.RemoveFileAssocs();
    }
    else if (!cliRoot.vVGMColl.empty())
    {
        // Fallback: export from the first valid collection
        VGMColl *coll = cliRoot.vVGMColl[0];
        if (formatFlags & VGMTRANS_FMT_SF2)
        {
            SF2File *sf2file = coll->CreateSF2File();
            if (sf2file != NULL)
            {
                std::wstring sf2filepath = cliRoot.saveDirPath + L"/" + wStem + L".sf2";
                sf2file->SaveSF2File(sf2filepath);
                delete sf2file;
            }
        }
        if (formatFlags & VGMTRANS_FMT_DLS)
        {
            DLSFile dlsfile;
            if (coll->CreateDLSFile(dlsfile))
            {
                std::wstring dlsfilepath = cliRoot.saveDirPath + L"/" + wStem + L".dls";
                dlsfile.SaveDLSFile(dlsfilepath);
            }
        }
    }

    cliRoot.Exit();
    return 0;
}

extern "C" int VgmtransConvertFile(const char *inFile, const char *outDir)
{
    return VgmtransConvertFileExt(inFile, outDir, VGMTRANS_FMT_SF2);
}
