#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN // <--- FIX 1: Silences the yellow terminal warnings!
#include <winsock2.h>
#include <windows.h>
#include <shlobj.h>   
#include <ole2.h>
#include <objbase.h>  
#include <gdiplus.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>    
#include <chrono>   
#include "httplib.h"

using namespace Gdiplus;

// helaps to find the linking of windows socket library
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

// store url and text before ios take it;
std::string stored_viewport_payload = "";

// ==============================================================================
// VIEWPORT HARDWARE AUTOMATOR
// ==============================================================================
void HandleViewportSync(std::string url, std::string text) {
    // 1. Convert incoming iOS UTF-8 text to Windows UTF-16 Wide String
    int wchars_num = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    std::wstring wtext(wchars_num, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wtext[0], wchars_num);

    // 2. Stage text to clipboard using CF_UNICODETEXT
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        
        // memSize must include the +1 for the null terminator in memory
        size_t memSize = (wtext.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, memSize);
        
        if (hMem) {
            memcpy(GlobalLock(hMem), wtext.c_str(), memSize);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem); // <--- Use Unicode format
        }
        CloseClipboard();
    }

    // 3. Build the message using Wide Strings (Notice the 'L' before the quotes)
    std::wstring wmsg = L"Do you want to sync the viewport to this text?\n\n\"" + wtext + L"\"";
    
    // 4. Use MessageBoxW ('W' for Wide/Unicode) instead of MessageBoxA
    int userChoice = MessageBoxW(NULL, wmsg.c_str(), L"Incoming Viewport Sync", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);

    if (userChoice == IDYES) {

        //Put only the text into clipbaord
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            
            // memSize must include the +1 for the null terminator in memory
            size_t memSize = (wtext.length() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, memSize);
            
            if (hMem) {
                memcpy(GlobalLock(hMem), wtext.c_str(), memSize);
                GlobalUnlock(hMem);
                
                // Use Unicode format so special characters and emojis paste perfectly
                SetClipboardData(CF_UNICODETEXT, hMem); 
            }
            CloseClipboard();
        }

        // Launch Brave
        std::string command = "start brave \"" + url + "\"";
        system(command.c_str());

        // Wait for page to load
        Sleep(3500); 

        INPUT inputs[4] = {0};
        
        // Press Ctrl + F
        inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
        inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'F';
        inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'F'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));

        Sleep(300); 

        // Press Ctrl + V
        inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
        inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V';
        inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));

        Sleep(100);

        // Press Enter
        inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_RETURN;
        inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = VK_RETURN; inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));

        // Wait 4 seconds, then press Escape to clear highlight
        Sleep(4000);
        INPUT esc[2] = {0};
        esc[0].type = INPUT_KEYBOARD; esc[0].ki.wVk = VK_ESCAPE;
        esc[1].type = INPUT_KEYBOARD; esc[1].ki.wVk = VK_ESCAPE; esc[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, esc, sizeof(INPUT));
    }
}

struct ViewportSyncArgs {
    std::string url;
    std::string text;
};

DWORD WINAPI RunViewportSync(LPVOID parameter) {
    std::unique_ptr<ViewportSyncArgs> args(static_cast<ViewportSyncArgs*>(parameter));
    HandleViewportSync(args->url, args->text);
    return 0;
}

int main(int argc, char* argv[]) {
    FreeConsole();
    
    // ==============================================================================
    // SECURITY GATEKEEPER
    // ==============================================================================
    std::string MASTER_PASSWORD = "";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-pw" && i + 1 < argc) {
            MASTER_PASSWORD = argv[i + 1];
            break;
        }
    }

    if (MASTER_PASSWORD.empty()) {
        MessageBoxA(NULL, "Server failed to start: You must provide a password using the -pw flag.", "Security Error", MB_ICONERROR | MB_OK);
        return 1; 
    }

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    httplib::Server svr;

    // ==============================================================================
    // ENDPOINT 1: PULL (Snipping Tool & Text)
    // ==============================================================================
    // Notice the [&] so it can read MASTER_PASSWORD
    svr.Get("/pull", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_header("X-Auth-Token") || req.get_header_value("X-Auth-Token") != MASTER_PASSWORD) {
            res.status = 401;
            res.set_content("Access Denied", "text/plain");
            return;
        }

        if (OpenClipboard(GetDesktopWindow())) {
            if (IsClipboardFormatAvailable(CF_BITMAP)) {
                HANDLE hData = GetClipboardData(CF_BITMAP);
                if (hData) {
                    Bitmap* bitmap = Bitmap::FromHBITMAP((HBITMAP)hData, NULL);
                    if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
                        CLSID pngClsid;
                        CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &pngClsid);
                        IStream* pStream = NULL;
                        if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) == 0) {
                            if (bitmap->Save(pStream, &pngClsid, NULL) == Gdiplus::Ok) {
                                HGLOBAL hGlobal = NULL;
                                GetHGlobalFromStream(pStream, &hGlobal);
                                if (hGlobal) {
                                    void* pData = GlobalLock(hGlobal);
                                    res.set_content(reinterpret_cast<const char*>(pData), GlobalSize(hGlobal), "image/png");
                                    GlobalUnlock(hGlobal);
                                }
                            }
                            pStream->Release();
                        }
                        delete bitmap;
                    }
                }
            } else if (IsClipboardFormatAvailable(CF_TEXT)) {
                HANDLE hData = GetClipboardData(CF_TEXT);
                if (hData) {
                    char* pszText = static_cast<char*>(GlobalLock(hData));
                    if (pszText) {
                        std::string text = pszText;
                        GlobalUnlock(hData);
                        res.set_content(text.c_str(), text.length(), "text/plain");
                    }
                }
            } else {
                res.set_content("No valid data.", "text/plain");
            }
            CloseClipboard();
        }
    });

    // ==============================================================================
    // ENDPOINT 2: PUSH TEXT
    // ==============================================================================
    svr.Post("/push", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_header("X-Auth-Token") || req.get_header_value("X-Auth-Token") != MASTER_PASSWORD) {
            res.status = 401;
            res.set_content("Access Denied", "text/plain");
            return;
        }

        std::string new_text = req.body; 
        
        if (OpenClipboard(nullptr)) {
            EmptyClipboard(); 
            
            // 1. Convert incoming iOS UTF-8 text to Windows UTF-16 Wide String
            int wchars_num = MultiByteToWideChar(CP_UTF8, 0, new_text.c_str(), -1, NULL, 0);
            std::wstring wtext(wchars_num, 0);
            MultiByteToWideChar(CP_UTF8, 0, new_text.c_str(), -1, &wtext[0], wchars_num);

            // 2. Allocate memory specifically sized for wide characters
            size_t memSize = wchars_num * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, memSize);
            
            if (hMem) {
                // 3. Lock, copy, and set using the Unicode format flag
                memcpy(GlobalLock(hMem), wtext.c_str(), memSize);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem); // <--- Upgraded to CF_UNICODETEXT
            }
            CloseClipboard();
        }
        res.set_content("Success", "text/plain");
    });

   // ==============================================================================
// ENDPOINT 3: PUSH VIEWPORT (URL + Text)
// ==============================================================================
svr.Post("/push_viewport", [&](const httplib::Request& req, httplib::Response& res) {
    if (!req.has_header("X-Auth-Token") || req.get_header_value("X-Auth-Token") != MASTER_PASSWORD) {
        res.status = 401;
        res.set_content("Access Denied", "text/plain");
        return;
    }

    std::string payload = req.body;
    size_t delimiter = payload.find("|||");
    size_t encoded_delimiter = payload.find("%7C%7C%7C");
    
    std::string url = "";
    std::string text = "";

    // Smart-parse to handle iOS automatic URL-encoding
    if (encoded_delimiter != std::string::npos) {
        url = payload.substr(0, encoded_delimiter);
        text = payload.substr(encoded_delimiter + 9); 
    } else if (delimiter != std::string::npos) {
        url = payload.substr(0, delimiter);
        text = payload.substr(delimiter + 3); 
    }
    
    if (!url.empty() && !text.empty()) {
        auto* args = new ViewportSyncArgs{url, text};
        HANDLE thread = CreateThread(nullptr, 0, RunViewportSync, args, 0, nullptr);
        
        if (thread) {
            CloseHandle(thread); 
            res.set_content("Viewport Sync Triggered", "text/plain");
        } else {
            delete args; 
            res.status = 500;
            res.set_content("Failed to start viewport sync", "text/plain");
        }
    } else {
        res.status = 400;
        res.set_content("Invalid Payload Format", "text/plain");
    }
});

    // ==============================================================================
    // ENDPOINT 4: STORE VIEWPORT (Windows -> Server Memory)
    // ==============================================================================
    svr.Post("/store_viewport", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_header("X-Auth-Token") || req.get_header_value("X-Auth-Token") != MASTER_PASSWORD) {
            res.status = 401; 
            res.set_content("Access Denied", "text/plain"); 
            return;
        }
        
        stored_viewport_payload = req.body; 
        res.set_content("Viewport Stored", "text/plain");
    });

    // ==============================================================================
    // ENDPOINT 5: PULL VIEWPORT (Server Memory -> iOS)
    // ==============================================================================
    svr.Get("/pull_viewport", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_header("X-Auth-Token") || req.get_header_value("X-Auth-Token") != MASTER_PASSWORD) {
            res.status = 401; 
            res.set_content("Access Denied", "text/plain"); 
            return;
        }
        
        if (stored_viewport_payload.empty()) {
            res.status = 404; 
            res.set_content("No viewport staged", "text/plain");
        } else {
            res.set_content(stored_viewport_payload, "text/plain");
            stored_viewport_payload = ""; // Instantly wipe payload after sending
        }
    });

    // ==============================================================================
    // ENDPOINT 6: PUSH IMAGE (Triple Format)
    // ==============================================================================
    svr.Post("/push_image", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_header("X-Auth-Token") || req.get_header_value("X-Auth-Token") != MASTER_PASSWORD) {
            res.status = 401;
            res.set_content("Access Denied", "text/plain");
            return;
        }

        const std::string& image_data = req.body;
        if (image_data.empty()) {
            res.status = 400;
            res.set_content("Empty payload", "text/plain");
            return;
        }

        if (OpenClipboard(GetDesktopWindow())) {
            EmptyClipboard();

            // ----------------------------------------------------------------------
            // 1. Web PNG (Fallback for Chromium browsers)
            // ----------------------------------------------------------------------
            UINT cfPNG = RegisterClipboardFormatA("PNG");
            HGLOBAL hPngMem = GlobalAlloc(GMEM_MOVEABLE, image_data.size());
            if (hPngMem) {
                memcpy(GlobalLock(hPngMem), image_data.data(), image_data.size());
                GlobalUnlock(hPngMem);
                SetClipboardData(cfPNG, hPngMem);
            }

            
            // ----------------------------------------------------------------------
            // 2. CF_DIB (The 100% Reliable Fix for Win+V Thumbnails)
            // ----------------------------------------------------------------------
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, image_data.size());
            if (hMem) {
                memcpy(GlobalLock(hMem), image_data.data(), image_data.size());
                GlobalUnlock(hMem);

                IStream* pStream = NULL;
                if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) == 0) {
                    Bitmap* bitmap = Bitmap::FromStream(pStream);
                    if (bitmap && bitmap->GetLastStatus() == Gdiplus::Ok) {
                        
                        // Save the GDI+ Bitmap to an in-memory BMP stream
                        IStream* pBmpStream = NULL;
                        if (CreateStreamOnHGlobal(NULL, TRUE, &pBmpStream) == 0) {
                            CLSID bmpClsid;
                            CLSIDFromString(L"{557cf400-1a04-11d3-9a73-0000f81ef32e}", &bmpClsid); // BMP Encoder
                            
                            if (bitmap->Save(pBmpStream, &bmpClsid, NULL) == Gdiplus::Ok) {
                                HGLOBAL hBmpGlobal = NULL;
                                GetHGlobalFromStream(pBmpStream, &hBmpGlobal);
                                if (hBmpGlobal) {
                                    SIZE_T bmpSize = GlobalSize(hBmpGlobal);
                                    void* pBmpData = GlobalLock(hBmpGlobal);
                                    
                                    // A valid BMP file is a 14-byte header followed by the pure DIB data.
                                    // Strip the first 14 bytes to get a structurally flawless CF_DIB.
                                    if (bmpSize > 14) {
                                        SIZE_T dibSize = bmpSize - 14;
                                        HGLOBAL hDIB = GlobalAlloc(GMEM_MOVEABLE, dibSize);
                                        if (hDIB) {
                                            memcpy(GlobalLock(hDIB), (BYTE*)pBmpData + 14, dibSize);
                                            GlobalUnlock(hDIB);
                                            SetClipboardData(CF_DIB, hDIB); 
                                            // Notice we removed CF_BITMAP entirely. Windows handles it automatically now.
                                        }
                                    }
                                    GlobalUnlock(hBmpGlobal);
                                }
                            }
                            pBmpStream->Release();
                        }
                    }
                    if (bitmap) delete bitmap;
                    pStream->Release();
                }
            }

            CloseClipboard();
        }
        res.set_content("Image Success", "text/plain");
    });

    svr.listen("0.0.0.0", 8080);
    
    Gdiplus::GdiplusShutdown(gdiplusToken); 
    return 0;
}