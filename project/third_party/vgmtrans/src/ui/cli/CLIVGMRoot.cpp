#include "pch.h"
#include "CLIVGMRoot.h"

void CLIVGMRoot::UI_SetRootPtr(VGMRoot** theRoot) {
    *theRoot = this;
}
void CLIVGMRoot::UI_Exit() {}
std::wstring CLIVGMRoot::UI_GetOpenFilePath(const std::wstring& suggestedFilename, const std::wstring& extension) { return L""; }
std::wstring CLIVGMRoot::UI_GetSaveFilePath(const std::wstring& suggestedFilename, const std::wstring& extension) { return L""; }
std::wstring CLIVGMRoot::UI_GetSaveDirPath(const std::wstring& suggestedDir) { return saveDirPath; }
