#pragma once
#include "Root.h"
#include <string>

class CLIVGMRoot : public VGMRoot {
public:
    CLIVGMRoot() {}
    virtual void UI_SetRootPtr(VGMRoot** theRoot);
    virtual void UI_Exit();
    virtual std::wstring UI_GetOpenFilePath(const std::wstring& suggestedFilename = L"", const std::wstring& extension = L"");
    virtual std::wstring UI_GetSaveFilePath(const std::wstring& suggestedFilename, const std::wstring& extension = L"");
    virtual std::wstring UI_GetSaveDirPath(const std::wstring& suggestedDir = L"");

    std::wstring saveDirPath;
};
