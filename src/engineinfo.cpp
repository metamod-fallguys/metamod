
// vi: set ts=4 sw=4 :
// vim: set tw=75 :

// engineinfo.cpp - get info about HL engine, like file used
//                  and test segment range
//


#ifdef _WIN32
// Don't include winspool.h; clashes with SERVER_EXECUTE from engine
#    define _WINSPOOL_H
#    include <windows.h>
#    include <winnt.h> // Header structures
#else
#    ifndef _GNU_SOURCE
#        define _GNU_SOURCE
#    endif
#    include <dlfcn.h> // dladdr()
#    include <link.h>  // ElfW(Phdr/Ehdr) macros.
                       // _DYNAMIC, r_debug, link_map, etc.

#    include <procmap/MemoryMap.hpp>

#endif /* _WIN32 */

#include <string.h> // strlen(), strrchr(), strcmp()
#include <stdio.h>  // printf()
#include <filesystem>
#include <fstream>
#include <iterator>

#include "blob.h"
#include "gamedata.h"

#include "engineinfo.h" // me
#include "log_meta.h"   // META_DEV()


void*    mutil_GetModuleBaseByHandle(DLHANDLE hModule);
size_t   mutil_GetImageSize(void* ImageBase);
DLHANDLE mutil_GetModuleHandle(const char* name);
void     mutil_CloseModuleHandle(DLHANDLE handle);

bool DLLINTERNAL EngineInfo::check_for_engine_module(const char* _pName)
{
    if (NULL == _pName) return false;

#ifdef _WIN32

    // The engine module is either sw.dll or hw.dll or swds.dll
    auto pC = strrchr(_pName, '.');

    pC -= 2;
    if (0 != strcmp(pC, "sw.dll") && 0 != strcmp(pC, "hw.dll"))
    {
        pC -= 2;
        if (0 != strcmp(pC, "swds.dll")) return false;
    }

    HMODULE hModule = GetModuleHandle(pC);
    if (NULL == hModule)
    {
        return false;
    }

    // Ok, we found the string sw(ds).dll, thus we deduct that this is the
    // name of the engine's shared object. We copy the string for future
    // reference and return successfully.
    size_t size = 0;
    while (*pC != '.' && size < c_EngineInfo__typeLen - 1)
    {
        m_type[size++] = *pC++;
    }
    m_type[size] = '\0';

#else /* _WIN32 */

    auto pC = strrchr(_pName, '.');

    pC -= 2;
    // Sven Co-op uses hw.so
    if (0 == strcmp(pC, "hw.so"))
    {
        strcpy(m_type, "hw.so");
        return true;
    }

    size_t size = strlen(_pName);

    if (size < 11)
    {
        // Forget it, this string is too short to even be 'engine_.so', so
        // it can't the name of the engine shared library.
        return false;
    }

    // Start parsing at the end. Since we know that the string is at least
    // 11 characters long we can safely do our parsing without being afraid
    // of leaving the string.
    pC = _pName + size - 1;

    // First we see if the string ends in '.so'
    if (*pC-- != 'o' || *pC-- != 's' || *pC-- != '.') return false;

    // Now find the '_' which would be the seperator between 'engine' and
    // the architecture.
    while (*pC != '_' && pC != _pName) --pC;
    if (pC == _pName) return false;

    // We are at the '_', thus the architecture identifier must start at
    // the next character.
    auto pType = pC + 1;

    // Now we walk further back and check if we find the string 'engine'
    // backwards.
    --pC;
    if (*pC-- != 'e' || *pC-- != 'n' || *pC-- != 'i' ||
        *pC-- != 'g' || *pC-- != 'n' || *pC != 'e')
    {
        return false;
    }

    // Ok, we found the string engine_*.so, thus we deduct that this is the
    // name of the engine's shared object. We copy the architecture string
    // for future reference and return successfully.
    size = 0;
    while (*pType != '.' && size < c_EngineInfo__typeLen - 1)
    {
        m_type[size++] = *pType++;
    }
    m_type[size] = '\0';


#endif /* _WIN32 */


    return true;
}


#ifdef _WIN32

int DLLINTERNAL EngineInfo::nthdr_module_name(void)
{
    PIMAGE_DOS_HEADER pDosHeader;
    PIMAGE_NT_HEADERS pNTHeader;
    unsigned char*    pBaseAddr;
    const char*       pName = "sw.dll";

    if (!check_for_engine_module(pName))
    {
        pName = "hw.dll";
        if (!check_for_engine_module(pName))
        {
            pName = "swds.dll";
            if (!check_for_engine_module(pName))
            {
                return MODULE_NAME_NOTFOUND;
            }
        }
    }

    // Get the module handle for the engine dll we found.
    // This is also the modules base address.
    HMODULE hModule = GetModuleHandle(pName);

    pBaseAddr = (unsigned char*)hModule;

    // Check if we find a DOS header
    pDosHeader = (PIMAGE_DOS_HEADER)hModule;
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return INVALID_DOS_SIGN;
    }

    // Check if we find a PE header
    pNTHeader = (PIMAGE_NT_HEADERS)(pBaseAddr + pDosHeader->e_lfanew);
    if (pNTHeader->Signature != IMAGE_NT_SIGNATURE)
    {
        return INVALID_NT_SIGN;
    }

    m_imageHandle = (DLHANDLE)pBaseAddr;

    set_code_range(pBaseAddr, pNTHeader);
    return 0;
}


int DLLINTERNAL EngineInfo::vac_pe_approx(enginefuncs_t* _pFuncs)
{
    if (!_pFuncs) return INVALID_ARG;
    char  executable[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, executable, sizeof(executable));
    if (!length || length >= sizeof(executable)) return NOTFOUND;
    const auto       directory = std::filesystem::path(executable).parent_path();
    Blob::Image      selected;
    std::string      source;
    constexpr size_t kMaxBlobFileSize   = 64 * 1024 * 1024;
    constexpr size_t kIdentityProbeSize = 32;
    const void*      probes[]           = {(void*)_pFuncs->pfnModelIndex, (void*)_pFuncs->pfnModelFrames,
                                           (void*)_pFuncs->pfnCVarGetFloat, (void*)_pFuncs->pfnCmd_Argv};
    for (const char* name : {"hw.dll", "sw.dll", "swds.dll"})
    {
        auto          path = directory / name;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0 || file.tellg() > std::streamoff(kMaxBlobFileSize)) continue;
        file.seekg(0);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
        Blob::Image                image;
        if (!Blob::Parse(bytes, image)) continue;
        bool matches = true;
        for (const auto& section : image.sections)
        {
            uintptr_t       cursor = section.address;
            const uintptr_t end    = cursor + section.size;
            while (cursor < end)
            {
                MEMORY_BASIC_INFORMATION memory{};
                if (!VirtualQuery((void*)cursor, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
                    (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                {
                    matches = false;
                    break;
                }
                cursor = uintptr_t(memory.BaseAddress) + memory.RegionSize;
            }
            if (!matches) break;
        }
        for (const void* probe : probes)
        {
            MEMORY_BASIC_INFORMATION memory{};
            if (!matches || !VirtualQuery(probe, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
                (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
                uintptr_t(probe) + kIdentityProbeSize > uintptr_t(memory.BaseAddress) + memory.RegionSize ||
                !Blob::MatchesCode(image, uintptr_t(probe), probe, kIdentityProbeSize))
            {
                matches = false;
                break;
            }
        }
        if (!matches) continue;
        if (!source.empty())
        {
            META_WARNING("Ambiguous BLOB engine files; refusing module identity");
            return NOTFOUND;
        }
        source   = path.string();
        selected = std::move(image);
    }
    if (source.empty())
    {
        META_WARNING("Unable to match a BLOB engine file to the loaded engine");
        return NOTFOUND;
    }
    std::vector<GameData::ImageRange> ranges;
    for (const auto& section : selected.sections)
        ranges.push_back({section.address - selected.base, section.size});
    void* base = (void*)uintptr_t(selected.base);
    if (!GameData::RegisterModuleFileSource(base, source, selected.size, ranges)) return NOTFOUND;
    m_imageStart = base;
    m_imageEnd   = (char*)base + selected.size;
    m_codeStart  = (void*)uintptr_t(selected.sections.front().address);
    m_codeEnd    = (char*)m_codeStart + selected.sections.front().size;
    strncpy(m_type, "blob", c_EngineInfo__typeLen);
    m_state = STATE_VALID;
    META_LOG("Matched BLOB engine %s at %p (%u bytes)", source.c_str(), base, selected.size);
    return 0;
}


void DLLINTERNAL EngineInfo::set_code_range(unsigned char* _pBase, PIMAGE_NT_HEADERS _pNThdr)
{
    m_codeStart = _pBase + _pNThdr->OptionalHeader.BaseOfCode;
    m_codeEnd   = _pBase + _pNThdr->OptionalHeader.BaseOfCode + _pNThdr->OptionalHeader.SizeOfCode;

    m_imageStart = _pBase;
    m_imageEnd   = _pBase + _pNThdr->OptionalHeader.SizeOfImage;

    m_state = STATE_VALID;
}


#else /* _WIN32 */

int DLLINTERNAL EngineInfo::phdr_elfhdr(const char* _pszFileName, void* _pElfHdr)
{
    ElfW(Ehdr)* pEhdr = (ElfW(Ehdr)*)_pElfHdr;
    ElfW(Phdr) * pPhdr;

    if (NULL == _pElfHdr) return INVALID_ARG;

    m_imageHandle = mutil_GetModuleHandle(_pszFileName);

    m_imageStart = _pElfHdr;
    m_imageEnd   = NULL;

    if (pEhdr->e_ident[0] == 0x7f && pEhdr->e_ident[1] == 'E' && pEhdr->e_ident[2] == 'L' && pEhdr->e_ident[3] == 'F')
    {

        // Looking good, we found an ELF signature.
        // Let's see if the rest of the data at this address would fit an ELH header, too.
        if ((pEhdr->e_ident[EI_CLASS] == ELFCLASS32 //ELFW(CLASS)
             || pEhdr->e_ident[EI_CLASS] == ELFCLASS64)

            && (pEhdr->e_ident[EI_DATA] == ELFDATA2LSB || pEhdr->e_ident[EI_DATA] == ELFDATA2MSB)

            && pEhdr->e_type == ET_DYN)
        {

            // Ok, we believe that this is a shared object's ELF header.
            // Let's find the program header for the segment that includes
            // the text section and return it.

            pPhdr = (ElfW(Phdr)*)((char*)pEhdr + pEhdr->e_phoff);
            for (int i = 0; i < pEhdr->e_phnum; i++, pPhdr++)
            {

                if (PT_LOAD == pPhdr->p_type && (pPhdr->p_flags & PF_R) && (pPhdr->p_flags & PF_X))
                {

                    // The text section gets loaded and has read and
                    // execute flags set. So this must be it.
                    set_code_range(pEhdr, pPhdr);
                    return 0;
                }
            }
        }
    }

    return HEADER_NOTFOUND;
}




int DLLINTERNAL EngineInfo::phdr_dladdr(void* _pMem)
{
    Dl_info info;

    if (0 != dladdr(_pMem, &info))
    {
        // Check if this is the engine module
        if (check_for_engine_module(info.dli_fname))
        {


            return phdr_elfhdr(info.dli_fname, info.dli_fbase);
        }
    }

    return NOTFOUND;
}


int DLLINTERNAL EngineInfo::phdr_r_debug(void)
{
    ElfW(Dyn) * pDyn;
    struct r_debug*  pr_debug = NULL;
    struct link_map* pMap;


    // Search if we have a DT_DEBUG symbol in our DYNAMIC segment, which
    // ought to be set to the r_debug structure's address.
    for (pDyn = _DYNAMIC; pDyn->d_tag != DT_NULL; ++pDyn)
    {
        if (pDyn->d_tag == DT_DEBUG)
        {
            pr_debug = (struct r_debug*)pDyn->d_un.d_ptr;
            break;
        }
    }

    if (DT_NULL == pDyn->d_tag)
    {
    }
    else if (NULL == pr_debug)
    {
    }
    else
    {
        pMap = pr_debug->r_map;

        // Walk to the start of the list
        while (pMap->l_prev != NULL) pMap = pMap->l_prev;
        do
        {
            if (check_for_engine_module(pMap->l_name))
            {
                return phdr_elfhdr(pMap->l_name, (void*)pMap->l_addr);
            }

            pMap = pMap->l_next;
        } while (NULL != pMap);
    }

    return NOTFOUND;
}

void DLLINTERNAL EngineInfo::set_code_range(void* _pBase, ElfW(Phdr) * _pPhdr)
{
    unsigned char* pBase = (unsigned char*)_pBase;
    m_codeStart          = pBase + _pPhdr->p_vaddr;
    m_codeEnd            = pBase + _pPhdr->p_vaddr + _pPhdr->p_memsz;

    m_state = STATE_VALID;
}


#endif /* _WIN32 */

void DLLINTERNAL EngineInfo::uninitialise()
{
    GameData::UnregisterModuleFileSource(m_imageStart);
    if (m_imageHandle)
    {
        mutil_CloseModuleHandle(m_imageHandle);
        m_imageHandle = NULL;
    }

    m_imageStart = 0;
    m_imageEnd   = 0;
    m_codeStart  = 0;
    m_codeEnd    = 0;
    m_state      = STATE_NULL;
}

int DLLINTERNAL EngineInfo::initialise(enginefuncs_t* _pFuncs)
{
    int ret = 0;

    // Have to do this only once.
    if (NULL != m_codeStart)
    {
        return 0;
    }

#ifdef _WIN32

    ret = nthdr_module_name();
    if (MODULE_NAME_NOTFOUND == ret && _pFuncs)
    {
        // BLOB images are not registered with the Windows module loader.
        ret = vac_pe_approx(_pFuncs);
    }

#else /* _WIN32 */

    // If we have no reference pointer to start from we can only try to use
    // the r_debug symbol.
    if (NULL == _pFuncs)
    {
        ret = phdr_r_debug();
    }

    // If we have a refererence pointer we try to use it first.
    else if (0 != phdr_dladdr(_pFuncs))
    {
        ret = phdr_r_debug();
    }

#endif /* _WIN32 */

    if (0 != ret)
    {
        m_state = STATE_INVALID;
        META_DEV("Unable to determine engine code address range!");
    }
    else
    {
        META_DEV("Set engine code range: start address = %p, end address = %p",
                 m_codeStart, m_codeEnd);
    }

    if (!m_imageStart && m_imageHandle)
    {
        m_imageStart = (decltype(m_imageStart))mutil_GetModuleBaseByHandle(m_imageHandle);
        m_imageEnd   = (decltype(m_imageEnd))((uintptr_t)m_imageStart + mutil_GetImageSize(m_imageHandle));
    }

    return 0;
}
