// dllmain.cpp : Defines the entry point for the DLL application.
#include <Windows.h>
#include <MinHook.h>
#include <iostream>

#if defined _M_X64
#pragma comment(lib, "libMinHook-x64-v141-mt")
#elif defined _M_IX86
#pragma comment(lib, "libMinHook-x86-v141-mt")
#endif

FILE* pFile = nullptr;

typedef BOOL (WINAPI *DeviceIoControlTYPE)(
    _In_ HANDLE hDevice,
    _In_ DWORD dwIoControlCode,
    _In_reads_bytes_opt_(nInBufferSize) LPVOID lpInBuffer,
    _In_ DWORD nInBufferSize,
    _Out_writes_bytes_to_opt_(nOutBufferSize, *lpBytesReturned) LPVOID lpOutBuffer,
    _In_ DWORD nOutBufferSize,
    _Out_opt_ LPDWORD lpBytesReturned,
    _Inout_opt_ LPOVERLAPPED lpOverlapped
);

// Original function
DeviceIoControlTYPE fpDeviceIoControl = NULL;

#pragma pack(push, 1)

// Detour function which overrides MessageBoxW.
BOOL WINAPI DetourIoControl(_In_ HANDLE hDevice,
    _In_ DWORD dwIoControlCode,
    _In_reads_bytes_opt_(nInBufferSize) LPVOID lpInBuffer,
    _In_ DWORD nInBufferSize,
    _Out_writes_bytes_to_opt_(nOutBufferSize, *lpBytesReturned) LPVOID lpOutBuffer,
    _In_ DWORD nOutBufferSize,
    _Out_opt_ LPDWORD lpBytesReturned,
    _Inout_opt_ LPOVERLAPPED lpOverlapped)
{

    #define DEF_FUNC fpDeviceIoControl(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned, lpOverlapped)

    switch (dwIoControlCode) {
    case 0x81112FF8: {
        if (lpInBuffer == nullptr)
            goto defaultRoutine;

        printf("::SetPCIConfig\n");
        // writes bus

        auto ret = DEF_FUNC;
        return ret;
    }
    case 0x81112F24: {
        if (lpInBuffer == nullptr || nInBufferSize < 0x14)
            goto defaultRoutine;

        struct data {
            unsigned int one, two, three, four, bufferLen;
            void* dataPointer;
        } *pk = reinterpret_cast<data*>(lpInBuffer);

        auto ret = DEF_FUNC;
        unsigned int stuff = pk->bufferLen > 0 ? *reinterpret_cast<int*>(pk->dataPointer) : 0;
        printf("::GetPCIConfig cpu %i: %X %X %X %X buffer: %X %X %f\n", (int)GetCurrentProcessorNumber(), pk->one, pk->two, pk->three, pk->four, pk->bufferLen, pk->bufferLen > 0 ? pk->dataPointer : lpInBuffer, (stuff >> 16) / 256.f);

        return ret;
    }
    case 0x81112EE0: {
        if (lpInBuffer == nullptr)
            goto defaultRoutine;

        struct data {
            int reqIndex;
            unsigned __int64 outData;
        } *pk = reinterpret_cast<data*>(lpInBuffer);
        /* index to this array: amd documentation: https://www.amd.com/system/files/TechDocs/56255_OSRR.pdf
dd 0C0010062h, 0C0010063h, 0C0010061h, 0C0010064h, 0C0010065h
dd 0C0010066h, 0C0010067h, 0C0010068h, 0C0010015h, 0E7h
dd 0E8h,       0C0010290h, 0C0010292h, 0C0010293h, 8Bh
        */

        auto ret = DEF_FUNC;
        switch (pk->reqIndex) {
        case 13: {// GetCurrentFrequency

            int stuff = pk->outData;
            printf("__readmsr ::GetCurrentFrequency cpu %i: freq=%f\n", (int)GetCurrentProcessorNumber(), (float)((pk->outData & 0xFF0000) >> 16) / 2.f, (float)(unsigned __int8)stuff / (float)((stuff >> 8) & 0x3F) * 200.0f);
            break;
        }
        case 3: {
            printf("__readmsr ::SetCurrentFID\n");
        }
        default:
            printf("__readmsr request %i %llX\n", pk->reqIndex, pk->outData);
        }
       
        return ret;
    }
    default: {
        printf("Unknown deviceIo %X\n", dwIoControlCode);
        break;
    }
    }
    
  defaultRoutine:
    return DEF_FUNC;
}

#pragma pack(pop)

LPVOID handle;
int properlyDetach() {
    printf("Detaching console...\n");
    FreeConsole();
    FreeLibraryAndExitThread(static_cast<HMODULE>(handle), 1);
    return 1;
}

DWORD WINAPI setup(LPVOID lpParam) {
    handle = lpParam;
    if (!AllocConsole()) return 0;
    freopen_s(&pFile, "CONOUT$", "w", stdout);

    printf("Injected!\n");
   
    if (MH_Initialize() != MH_OK)
    {
        printf("MH_Initialize not ok!\n");
        properlyDetach();
    }

    if (MH_CreateHookApi(L"Kernel32", "DeviceIoControl", &DetourIoControl, reinterpret_cast<LPVOID*>(&fpDeviceIoControl)) != MH_OK) {
        printf("MH_CreateHookApi error!\n");
        properlyDetach();
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        printf("MH_EnableHook not ok!\n");
        properlyDetach();
    }

    printf("properly hooked!\n");

    Sleep(25000);

    if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK)
    {
        printf("MH_DisableHook not ok!\n");
        properlyDetach();
    }

    if (MH_Uninitialize() != MH_OK)
    {
        printf("MH_Uninitialize not ok!\n");
        properlyDetach();
    }

    properlyDetach();
    return 0;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)setup, hModule, NULL, NULL);
        
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

