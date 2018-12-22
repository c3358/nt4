/*++

Copyright (c) 1995  Microsoft Corporation

Module Name:

    line20.c

Abstract:

    This module contains functions for testing Telephony line device APIs.

Author:

    Xiao Ying Ding (XiaoD)    30-Jan-1996

Revision History:

--*/


#include <windows.h>
#include "tapi.h"
#include "trapper.h"
#include "doline.h"
#include "dophone.h"
#include "tcore.h"
#include "vars.h"
#include "line20.h"


// Put a module usage counter in a shared data section.
// Its value will be used to determine whether or not the ttest dll
// and the tcore dll should be loaded/freed during a process
// attach/detach, respectively.
#pragma data_seg("Shared")

LONG glModuleUsage = 0;

#pragma data_seg()


// Instruct the linker to make the Shared section readable,
// writable, and shared.
#pragma data_seg(".drectve")
    static char szLinkDirectiveShared[] = "-section:Shared,rws";
#pragma data_seg()


// Globals for storing dll handles and the log function pointer
HANDLE ghline20Dll    = NULL;
LOGPROC glpfnLogProc = NULL;
CHAR gszTcoreDll[]   = "tcore";
CHAR gszTtestDll[]   = "ttest";


BOOL
WINAPI
line20DllMain(
    HANDLE  hDLL,
    DWORD   dwReason,
    LPVOID  lpReserved
    )
{
    HANDLE hTcoreDll;
    HANDLE hTtestDll;
    BOOL fFreeOK;

    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:

            // Increment this module's usage count
            InterlockedIncrement((PLONG) &glModuleUsage);

            ghline20Dll = hDLL;

            // Load the tcore dll and the ttest dll if this is the
            // first process to attach and if the dlls haven't already
            // been mapped into this process's address space.
            if (glModuleUsage == 1)
            {
                if (GetModuleHandle(gszTcoreDll) == NULL);
                {
                    hTcoreDll = LoadLibrary(gszTcoreDll);
                }

                if (GetModuleHandle(gszTtestDll) == NULL);
                {
                    hTtestDll = LoadLibrary(gszTtestDll);
                }

                if (hTcoreDll == NULL || hTtestDll == NULL)
                {
                    return FALSE;
                }
            }

            break;

        case DLL_THREAD_ATTACH:

            break;

        case DLL_THREAD_DETACH:

            break;

        case DLL_PROCESS_DETACH:

            // Decrement this module's usage count
            InterlockedDecrement((PLONG) &glModuleUsage);

            // Free the tcore dll if this is the last process
            // to detach and if the core dll is mapped into this
            // process's address space.
            if (glModuleUsage == 0)
            {
                if ((hTcoreDll = GetModuleHandle(gszTcoreDll)) != NULL)
                {
                    fFreeOK = FreeLibrary(hTcoreDll);
                }

                if ((hTtestDll = GetModuleHandle(gszTtestDll)) != NULL)
                {
                    return ( fFreeOK && (FreeLibrary(hTtestDll)) );
                }
            }

            break;
    }

    return TRUE;
}


BOOL
WINAPI
SuiteInit(
    LOGPROC pfnLog
    )
{
    // Store the log function pointer and pass it to the core dll.
    // Return whether or not the core suite init function succeeds.
    glpfnLogProc = pfnLog;
    return (TcoreSuiteInit(pfnLog));
}


BOOL
WINAPI
SuiteShutdown(
    void
    )
{
    return TRUE;
}


BOOL
WINAPI
SuiteAbout(
    HWND    hwndOwner
    )
{
    MessageBox (hwndOwner, "xxx", "About the Interface Test Suite", MB_OK);

    return TRUE;
}


BOOL
WINAPI
SuiteConfig(
    HWND    hwndOwner
    )
{
    MessageBox (hwndOwner, "xxx", "Interface Test Suite Config", MB_OK);

    return TRUE;
}


BOOL
WINAPI
Test1(
    HINSTANCE   hAppInst
    )
{
    return (TestLineAgentSpecific(TRUE, FALSE));
}


BOOL
WINAPI
Test2(
    HINSTANCE   hAppInst
    )
{
    return (TestLineGetAgentCaps(TRUE, FALSE));
}


BOOL
WINAPI
Test3(
    HINSTANCE   hAppInst
    )
{
    return (TestLineGetAgentActivityList(TRUE, FALSE));
}


BOOL
WINAPI
Test4(
    HINSTANCE   hAppInst
    )
{
    return (TestLineGetAgentGroupList(TRUE, FALSE));
}


BOOL
WINAPI
Test5(
    HINSTANCE   hAppInst
    )
{
    return (TestLineGetAgentStatus(TRUE, FALSE));
}


BOOL
WINAPI
Test6(
    HINSTANCE   hAppInst
    )
{
    return (TestLineProxyMessage(TRUE, FALSE));
}


BOOL
WINAPI
Test7(
    HINSTANCE   hAppInst
    )
{
    return (TestLineProxyResponse(TRUE, FALSE));
}


BOOL
WINAPI
Test8(
    HINSTANCE   hAppInst
    )
{
    return (TestLineSetAgentActivity(TRUE, FALSE));
}


BOOL
WINAPI
Test9(
    HINSTANCE   hAppInst
    )
{
    return (TestLineSetAgentGroup(TRUE, FALSE));
}


BOOL
WINAPI
Test10(
    HINSTANCE   hAppInst
    )
{
    return (TestLineSetAgentState(TRUE, FALSE));
}


BOOL
WINAPI
Test11(
    HINSTANCE   hAppInst
    )
{
    return (TestLineInitializeEx(TRUE, FALSE));
}


BOOL
WINAPI
Test12(
    HINSTANCE   hAppInst
    )
{
    return (TestLineGetMessage(TRUE, FALSE));
}




BOOL
ShouldTapiPhoneTestAbort(BOOL fQuiteMode)
{
	return FALSE;
}