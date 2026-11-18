/* ts_types.h - data types the decompile of n64dll.dll names.  The engine's C++ classes are Ghidra "placeholder"
 * structures (no layout recovered): the decompile addresses them by byte offsets, so they are bytes here.
 * Windows x64 types: LLP64 (long = 4 bytes). */
#ifndef TS_TYPES_H
#define TS_TYPES_H
#include "ghidra.h"
typedef char *LPCSTR; typedef char *LPSTR; typedef void *HWND; typedef uint32_t COLORREF; typedef int32_t BOOL;
typedef int32_t clock_t_ms; typedef uint64_t __uint64; typedef unsigned char ThrowInfo; typedef unsigned char tagMSG;
typedef void _func_void_ptr_void_ptr(void *);
typedef uint64_t int6; typedef uint64_t uint6; typedef uint64_t int7; typedef uint64_t uint7; typedef uint32_t int3;
typedef unsigned char CMFCRibbonProgressBar;
typedef unsigned char COleControlSiteOrWnd;
typedef unsigned char COleDispParams;
typedef unsigned char Dpoint2;
typedef unsigned char Dvector2;
typedef unsigned char EHExceptionRecord;
typedef unsigned char GlobalCore;
typedef unsigned char Point_2D;
typedef unsigned char _Cvtvec;
typedef unsigned char _Fac_node;
typedef unsigned char _Fac_tidy_reg_t;
typedef unsigned char _Init_atexit;
typedef unsigned char _Init_locks;
typedef unsigned char _LocaleUpdate;
typedef unsigned char _Locimp;
typedef unsigned char _Locinfo;
typedef unsigned char _Lockit;
typedef unsigned char _Mutex;
typedef unsigned char _s_CatchableType;
typedef unsigned char _xDISPATCHER_CONTEXT;
typedef unsigned char angularCmap;
typedef unsigned char clusterElement;
typedef unsigned char clusterElementPosn;
typedef unsigned char error_category;
typedef unsigned char error_code;
typedef unsigned char error_condition;
typedef unsigned char id;
typedef unsigned char locale;
typedef unsigned char negCollar;
typedef unsigned char nestInputDrawing;
typedef unsigned char nestParameters;
typedef unsigned char nestedSheetResult;
typedef unsigned char optExctNestedPart;
typedef unsigned char optFlags;
typedef unsigned char optStruct;
typedef unsigned char partDrawing;
typedef unsigned char partGeomLib;
typedef unsigned char partPosnOnSheet;
typedef unsigned char partSpecificCutterDia;
typedef unsigned char partToBeNested;
typedef unsigned char partTriedDetails;
typedef unsigned char partType;
typedef unsigned char shakeNest;
typedef unsigned char sheetProfArea;
typedef unsigned char stringdispid;
typedef unsigned char trialSheet;
#endif
