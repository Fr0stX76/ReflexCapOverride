OPTION CASEMAP:NONE

EXTERN g_exports:QWORD

VERSION_FORWARD MACRO name, slot
name PROC
    jmp QWORD PTR [g_exports + slot * 8]
name ENDP
ENDM

.CODE

VERSION_FORWARD GetFileVersionInfoA,        0
VERSION_FORWARD GetFileVersionInfoByHandle, 1
VERSION_FORWARD GetFileVersionInfoExA,      2
VERSION_FORWARD GetFileVersionInfoExW,      3
VERSION_FORWARD GetFileVersionInfoSizeA,    4
VERSION_FORWARD GetFileVersionInfoSizeExA,  5
VERSION_FORWARD GetFileVersionInfoSizeExW,  6
VERSION_FORWARD GetFileVersionInfoSizeW,    7
VERSION_FORWARD GetFileVersionInfoW,        8
VERSION_FORWARD VerFindFileA,               9
VERSION_FORWARD VerFindFileW,               10
VERSION_FORWARD VerInstallFileA,            11
VERSION_FORWARD VerInstallFileW,            12
VERSION_FORWARD VerLanguageNameA,           13
VERSION_FORWARD VerLanguageNameW,           14
VERSION_FORWARD VerQueryValueA,             15
VERSION_FORWARD VerQueryValueIndexA,        16
VERSION_FORWARD VerQueryValueIndexW,        17
VERSION_FORWARD VerQueryValueW,             18

END
