/***************************************************************************
 *   Copyright (C) 2012-2024 by Terraneo Federico                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   As a special exception, if other files instantiate templates or use   *
 *   macros or inline functions from this file, or you compile this file   *
 *   and link it with other works to produce a work based on this file,    *
 *   this file does not by itself cause the resulting work to be covered   *
 *   by the GNU General Public License. However the source code for this   *
 *   file must still be made available in accordance with the GNU General  *
 *   Public License. This exception does not invalidate any other reasons  *
 *   why a work based on this file might be covered by the GNU General     *
 *   Public License.                                                       *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>   *
 ***************************************************************************/

#include "elf_program.h"
#include "process_pool.h"
#include "filesystem/file_access.h"
#include <stdexcept>
#include <cstring>
#include <cstdio>

using namespace std;

#ifdef WITH_PROCESSES

namespace miosix {

///\internal Enable/disable debugging of program loading
//#define DBG iprintf
#define DBG(x,...) do {} while(0)
    
///By convention, in an elf file for Miosix, the data segment starts @ this addr
static const unsigned int DATA_BASE=0x40000000;

//
// class ElfProgram
//

ElfProgram::ElfProgram(const char *path) : elf(nullptr), size(0), ec(-ENOEXEC)
{
    if(path==nullptr || path[0]=='\0')
    {
        ec=-EFAULT;
        return;
    }
    //TODO: expand ./program using cwd of correct file descriptor table
    string filePath=path;
    ResolvedPath openData=FilesystemManager::instance().resolvePath(filePath);
    if(openData.result<0)
    {
        ec=-ENOENT;
        return;
    }
    StringPart relativePath(filePath,string::npos,openData.off);
    intrusive_ref_ptr<FileBase> file;
    if(int res=openData.fs->open(file,relativePath,O_RDONLY,0)<0)
    {
        ec=res;
        return;
    }
    MemoryMappedFile mmFile=file->getFileFromMemory();
    if(mmFile.isValid()==false)
    {
        //TODO: load to RAM for filesystems incapable of XIP
        ec=-EFAULT;
        return;
    }
    elf=reinterpret_cast<const unsigned int*>(mmFile.data);
    size=mmFile.size;
    validateHeader();
}

ElfProgram::ElfProgram(const unsigned int *elf, unsigned int size)
    : elf(elf), size(size), ec(-ENOEXEC)
{
    validateHeader();
}

void ElfProgram::validateHeader()
{
    //Validate ELF header
    //Note: this code assumes a little endian elf and a little endian ARM CPU
    if(isUnaligned(getElfBase(),8))
    {
        DBG("Elf file load address alignment error");
        return;
    }
    if(size<sizeof(Elf32_Ehdr)) return;
    const Elf32_Ehdr *ehdr=getElfHeader();
    static const char magic[EI_NIDENT]={0x7f,'E','L','F',1,1,1};
    if(memcmp(ehdr->e_ident,magic,EI_NIDENT))
    {
        DBG("Unrecognized format");
        return;
    }
    if(ehdr->e_type!=ET_EXEC) return;
    if(ehdr->e_machine!=EM_ARM)
    {
        DBG("Wrong CPU arch");
        return;
    }
    if(ehdr->e_version!=EV_CURRENT) return;
    if(ehdr->e_entry>=size) return;
    if(ehdr->e_phoff>=size-sizeof(Elf32_Phdr)) return;
    if(isUnaligned(ehdr->e_phoff,4)) return;
    // Old GCC 4.7.3 used to set bit 0x2 (EF_ARM_HASENTRY) but there's no trace
    // of this requirement in the current ELF spec for ARM.
    if((ehdr->e_flags & EF_ARM_EABIMASK) != EF_ARM_EABI_VER5) return;
    #if !defined(__FPU_USED) || __FPU_USED==0
    if(ehdr->e_flags & EF_ARM_VFP_FLOAT)
    {
        DBG("FPU required");
        return;
    }
    #endif
    if(ehdr->e_ehsize!=sizeof(Elf32_Ehdr)) return;
    if(ehdr->e_phentsize!=sizeof(Elf32_Phdr)) return;
    //This to avoid that the next condition could pass due to 32bit wraparound
    //20 is an arbitrary number, could be increased if required
    if(ehdr->e_phnum>20) return;
    if(ehdr->e_phoff+(ehdr->e_phnum*sizeof(Elf32_Phdr))>size) return;
    
    //Validate program header table
    bool codeSegmentPresent=false;
    bool dataSegmentPresent=false;
    bool dynamicSegmentPresent=false;
    int dataSegmentSize=0;
    const Elf32_Phdr *phdr=getProgramHeaderTable();
    for(int i=0;i<getNumOfProgramHeaderEntries();i++,phdr++)
    {
        //The third condition does not imply the other due to 32bit wraparound
        if(phdr->p_offset>=size) return;
        if(phdr->p_filesz>=size) return;
        if(phdr->p_offset+phdr->p_filesz>size) return;
        switch(phdr->p_align)
        {
            case 0: break;
            case 1: break;
            //NOTE: the elf spec states explicitly p_align must be a power of 2,
            //and the isUnaligned function only works if the alignment is a
            //power of 2. On top of that, we also want to whitelist the
            //supported alignments. Increasing alignment requires to also
            //increase the RomFs image alignment in romfs_types.h and mkimage.pl
            //The choice to support up to 64 bytes is because some ARM
            //architectures, such as Cortex-M4 result in elf files with the text
            //segment requiring 64 byte alignment. This was traced to some
            //library functions, such as strlen and strcmp whose code is
            //specified to require up to 64 byte alignment. Some tests were made
            //with strlen and it looks like the code works also unaligned, so
            //maybe the devs of that code (ARM) wanted to align it to the cache
            //line of *their* machine? In any case elf files end up exisitng
            //whose text segments have up to 64 byte aligment and to play it
            //safe we support that.
            case 2:
            case 4:
            case 8:
            case 16:
            case 32:
            case 64:
                if(isUnaligned(phdr->p_offset,phdr->p_align))
                {
                    DBG("Alignment error");
                    return;
                }
                break;
            default:
                DBG("Unsupported segment alignment");
                return;
        }
        
        switch(phdr->p_type)
        {
            case PT_LOAD:
                if(phdr->p_flags & ~(PF_R | PF_W | PF_X)) return;
                if(!(phdr->p_flags & PF_R)) return;
                if((phdr->p_flags & PF_W) && (phdr->p_flags & PF_X))
                {
                    DBG("File violates W^X");
                    return;
                }
                if(phdr->p_flags & PF_X)
                {
                    if(codeSegmentPresent) return; //Can't apper twice
                    codeSegmentPresent=true;
                    if(ehdr->e_entry<phdr->p_offset ||
                       ehdr->e_entry>phdr->p_offset+phdr->p_filesz ||
                       phdr->p_filesz!=phdr->p_memsz) return;
                }
                if((phdr->p_flags & PF_W) && !(phdr->p_flags & PF_X))
                {
                    if(dataSegmentPresent) return; //Two data segments?
                    dataSegmentPresent=true;
                    if(phdr->p_memsz<phdr->p_filesz) return;
                    unsigned int maxSize=MAX_PROCESS_IMAGE_SIZE-
                        MIN_PROCESS_STACK_SIZE;
                    if(phdr->p_memsz>=maxSize)
                    {
                        DBG("Data segment too big");
                        return;
                    }
                    dataSegmentSize=phdr->p_memsz;
                }
                break;
            case PT_DYNAMIC:
                if(dynamicSegmentPresent) return; //Two dynamic segments?
                dynamicSegmentPresent=true;
                //DYNAMIC segment *must* come after data segment
                if(dataSegmentPresent==false) return;
                if(phdr->p_align<4) return;
                if(validateDynamicSegment(phdr,dataSegmentSize)==false) return;
                break;
            default:
                //Ignoring other segments
                break;
        }
    }
    if(codeSegmentPresent==false) return; //Can't not have code segment
    // All checks passed setting error code to 0
    ec=0;
}

bool ElfProgram::validateDynamicSegment(const Elf32_Phdr *dynamic,
        unsigned int dataSegmentSize)
{
    unsigned int base=getElfBase();
    const Elf32_Dyn *dyn=reinterpret_cast<const Elf32_Dyn*>(base+dynamic->p_offset);
    const int dynSize=dynamic->p_memsz/sizeof(Elf32_Dyn);
    Elf32_Addr dtRel=0;
    Elf32_Word dtRelsz=0;
    unsigned int hasRelocs=0;
    bool miosixTagFound=false;
    unsigned int ramSize=0;
    unsigned int stackSize=0;
    for(int i=0;i<dynSize;i++,dyn++)
    {
        switch(dyn->d_tag)
        {
            case DT_REL:
                hasRelocs |= 0x1;
                dtRel=dyn->d_un.d_ptr;
                break;
            case DT_RELSZ:
                hasRelocs |= 0x2;
                dtRelsz=dyn->d_un.d_val;
                break;
            case DT_RELENT:
                hasRelocs |= 0x4;
                if(dyn->d_un.d_val!=sizeof(Elf32_Rel)) return false;
                break;  
            case DT_MX_ABI:
                if(dyn->d_un.d_val==DV_MX_ABI_V1) miosixTagFound=true;
                else {
                    DBG("Unknown/unsupported DT_MX_ABI");
                    return false;
                }
                break;
            case DT_MX_RAMSIZE:
                ramSize=dyn->d_un.d_val;
                break;
            case DT_MX_STACKSIZE:
                stackSize=dyn->d_un.d_val;
                break;
            case DT_RELA:
            case DT_RELASZ:
            case DT_RELAENT:
                DBG("RELA relocations unsupported");
                return false;
            default:
                //Ignore other entries
                break;
        }
    }
    if(miosixTagFound==false)
    {
        DBG("Not a Miosix executable");
        return false;
    }
    if(stackSize<MIN_PROCESS_STACK_SIZE)
    {
        DBG("Requested stack is too small");
        return false;
    }
    if(ramSize>MAX_PROCESS_IMAGE_SIZE)
    {
        DBG("Requested image size is too large");
        return false;
    }
    //NOTE: this check can only guarantee that statically data and stack fit
    //in the ram size. However the size for argv and envp that are pushed before
    //the stack (without contributing to the stack size) isn't known at this
    //point, so the memory can still be insufficient. Usually this is not an
    //issue since the ram size is oversized to leave room for the heap.
    if((stackSize & (CTXSAVE_STACK_ALIGNMENT-1)) ||
       (ramSize & 0x3) ||
       (ramSize < ProcessPool::blockSize) ||
       (stackSize>MAX_PROCESS_IMAGE_SIZE) ||
       (dataSegmentSize>MAX_PROCESS_IMAGE_SIZE) ||
       (dataSegmentSize+stackSize+WATERMARK_LEN>ramSize))
    {
        DBG("Invalid stack or RAM size");
        return false;
    }
    
    if(hasRelocs!=0 && hasRelocs!=0x7) return false;
    if(hasRelocs)
    {
        //The third condition does not imply the other due to 32bit wraparound
        if(dtRel>=size) return false;
        if(dtRelsz>=size) return false;
        if(dtRel+dtRelsz>size) return false;
        if(isUnaligned(dtRel,4)) return false;
        
        const Elf32_Rel *rel=reinterpret_cast<const Elf32_Rel*>(base+dtRel);
        const int relSize=dtRelsz/sizeof(Elf32_Rel);
        for(int i=0;i<relSize;i++,rel++)
        {
            switch(ELF32_R_TYPE(rel->r_info))
            {
                case R_ARM_NONE:
                    break;
                case R_ARM_RELATIVE:
                    if(rel->r_offset<DATA_BASE) return false;
                    if(rel->r_offset>DATA_BASE+dataSegmentSize-4) return false;
                    if(rel->r_offset & 0x3) return false;
                    break;
                default:
                    DBG("Unexpected relocation type");
                    return false;
            }
        }
    }
    return true;
}

ElfProgram& ElfProgram::operator= (ElfProgram&& rhs)
{
    //Move rhs fields into *this
    elf=rhs.elf;
    size=rhs.size;
    ec=rhs.ec;
    //Invalidate rhs
    rhs.elf=nullptr;
    rhs.size=0;
    rhs.ec=-ENOEXEC;
    return *this;
}

//
// class ProcessImage
//

void ProcessImage::load(const ElfProgram& program)
{
    if(image) ProcessPool::instance().deallocate(image);
    const unsigned int base=program.getElfBase();
    const Elf32_Phdr *phdr=program.getProgramHeaderTable();
    const Elf32_Phdr *dataSegment=nullptr;
    Elf32_Addr dtRel=0;
    Elf32_Word dtRelsz=0;
    bool hasRelocs=false;
    for(int i=0;i<program.getNumOfProgramHeaderEntries();i++,phdr++)
    {
        switch(phdr->p_type)
        {
            case PT_LOAD:
                if((phdr->p_flags & PF_W) && !(phdr->p_flags & PF_X))
                    dataSegment=phdr;
                break;
            case PT_DYNAMIC:
            {
                const Elf32_Dyn *dyn=reinterpret_cast<const Elf32_Dyn*>
                    (base+phdr->p_offset);
                const int dynSize=phdr->p_memsz/sizeof(Elf32_Dyn);
                for(int i=0;i<dynSize;i++,dyn++)
                {
                    switch(dyn->d_tag)
                    {
                        case DT_REL:
                            hasRelocs=true;
                            dtRel=dyn->d_un.d_ptr;
                            break;
                        case DT_RELSZ:
                            hasRelocs=true;
                            dtRelsz=dyn->d_un.d_val;
                            break;
                        case DT_MX_RAMSIZE:
                            size=dyn->d_un.d_val;
                            image=ProcessPool::instance()
                                    .allocate(dyn->d_un.d_val);
                        case DT_MX_STACKSIZE:
                            mainStackSize=dyn->d_un.d_val;
                            break;
                        default:
                            break;
                    }
                }
                break;
            }
            default:
                //Ignoring other segments
                break;
        }
    }
    const char *dataSegmentInFile=
        reinterpret_cast<const char*>(base+dataSegment->p_offset);
    char *dataSegmentInMem=reinterpret_cast<char*>(image);
    memcpy(dataSegmentInMem,dataSegmentInFile,dataSegment->p_filesz);
    dataSegmentInMem+=dataSegment->p_filesz;
    //Zero only .bss section (faster but processes leak data to other processes)
    //memset(dataSegmentInMem,0,dataSegment->p_memsz-dataSegment->p_filesz);
    //Zero the entire process image to prevent data leakage, exclude .data as
    //it is initialized, and the stack size since it will be filled later
    //NOTE: as the args block size isn't known here, we can't account for that.
    //This is not an issue though, we may just unnecessary fill with zeros up to
    //MAX_PROCESS_ARGS_BLOCK_SIZE bytes into the stack
    memset(dataSegmentInMem,0,size-dataSegment->p_filesz-mainStackSize-WATERMARK_LEN);
    dataBssSize=dataSegment->p_memsz;
    if(hasRelocs)
    {
        const Elf32_Rel *rel=reinterpret_cast<const Elf32_Rel*>(base+dtRel);
        const int relSize=dtRelsz/sizeof(Elf32_Rel);
        const unsigned int ramBase=reinterpret_cast<unsigned int>(image);
        DBG("Relocations -- start (code base @0x%x, data base @ 0x%x)\n",base,ramBase);
        for(int i=0;i<relSize;i++,rel++)
        {
            unsigned int offset=(rel->r_offset-DATA_BASE)/4;
            switch(ELF32_R_TYPE(rel->r_info))
            {
                case R_ARM_RELATIVE:
                    if(image[offset]>=DATA_BASE)
                    {
                        DBG("R_ARM_RELATIVE offset 0x%x from 0x%x to 0x%x\n",
                            offset*4,image[offset],image[offset]+ramBase-DATA_BASE);
                        image[offset]+=ramBase-DATA_BASE;
                    } else {
                        DBG("R_ARM_RELATIVE offset 0x%x from 0x%x to 0x%x\n",
                            offset*4,image[offset],image[offset]+base);
                        image[offset]+=base;
                    }
                    break;
                default:
                    break;
            }
        }
        DBG("Relocations -- end\n");
    }
}

ProcessImage::~ProcessImage()
{
    if(image) ProcessPool::instance().deallocate(image);
}

} //namespace miosix

#endif //WITH_PROCESSES
