/*
 * Minimal shared object that looks like a game DLL to is_gamedll(): it exports
 * GiveFnptrsToDll() and GetEntityAPI2(), which is all the ELF scan in
 * osdep_detect_gamedll_linux.cpp looks for. It is never dlopen()ed.
 */

static volatile char fake_gamedll_padding[64];

void GiveFnptrsToDll(void* pFuncs, void* pGlobals)
{
    (void)pFuncs;
    (void)pGlobals;
    (void)fake_gamedll_padding;
}

int GetEntityAPI2(void* pTable, int* version)
{
    (void)pTable;
    (void)version;
    return 1;
}
