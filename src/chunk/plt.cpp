#include <cassert>
#include <cstring>  // for memcpy
#include "plt.h"
#include "ifunc.h"
#include "function.h"
#include "module.h"
#include "external.h"
#include "serializer.h"
#include "visitor.h"
#include "dump.h"
#include "chunk/cache.h"
#include "operation/mutator.h"
#include "elf/elfspace.h"
#include "elf/symbol.h"
#include "instr/writer.h"
#include "disasm/disassemble.h"

#undef DEBUG_GROUP
#define DEBUG_GROUP dplt
#include "log/log.h"
#include "log/temp.h"
#include "chunk/dump.h"

class PLTRegistry {
private:
    typedef std::map<address_t, Reloc *> RegistryType;
    RegistryType registry;
public:
    void add(address_t address, Reloc *r) { registry[address] = r; }
    Reloc *find(address_t address);
};
Reloc *PLTRegistry::find(address_t address) {
    auto it = registry.find(address);
    return (it != registry.end() ? (*it).second : nullptr);
}

PLTTrampoline::PLTTrampoline(Chunk *pltList, address_t address,
    ExternalSymbol *externalSymbol, address_t gotPLTEntry, bool pltGot)
    : externalSymbol(externalSymbol),
    gotPLTEntry(gotPLTEntry), cache(nullptr), pltGot(pltGot) {

    setPosition(new AbsolutePosition(address));
    setParent(pltList);
}

std::string PLTTrampoline::getName() const {
    assert(externalSymbol != nullptr);
    return externalSymbol->getName() + std::string("@plt");
}

Chunk *PLTTrampoline::getTarget() const {
    assert(externalSymbol != nullptr);
    return externalSymbol->getResolved();
}

bool PLTTrampoline::isIFunc() const {
#ifdef ARCH_X86_64
    if(auto v = dynamic_cast<Function *>(getTarget())) {
        return v->isIFunc();
    }
#endif

    return false;
}

void PLTTrampoline::writeTo(std::string &target) {
#ifdef ARCH_X86_64
    bool isIFunc = this->isIFunc();
    if(externalSymbol) {
        LOG(1, "making PLT entry for [" << externalSymbol->getName()
            << "] : ifunc? " << (isIFunc ? "yes":"no"));
    }

    for(auto block : CIter::children(this)) {
        for(auto instr : CIter::children(block)) {
            LOG(1, "PLT instruction here!");
            InstrWriterCppString writer(target);
            instr->getSemantic()->accept(&writer);
        }
    }
#elif defined(ARCH_AARCH64) || defined(ARCH_ARM)
    auto originalSize = target.length();
    const size_t ARMPLTSize = PLTList::getPLTTrampolineSize();
    target.resize(originalSize + ARMPLTSize);
    char *output = target.data() + originalSize;
    writeTo(output);
#endif
}

void PLTTrampoline::writeTo(char *target) {
#ifdef ARCH_X86_64
    bool isIFunc = this->isIFunc();
    if(externalSymbol) {
        LOG(1, "making PLT entry for [" << externalSymbol->getName()
            << "] : ifunc? " << (isIFunc ? "yes":"no"));
    }

    LOG(11, "    write plt at address 0x" << std::hex << getAddress());
    for(auto block : CIter::children(this)) {
        LOG(11, "    write plt block at address 0x" << std::hex << block->getAddress());
        for(auto instr : CIter::children(block)) {
            char *output = reinterpret_cast<char *>(instr->getAddress());
            LOG(11, "    write plt instruction at address 0x" << std::hex << instr->getAddress());
            InstrWriterCString writer(output);
            instr->getSemantic()->accept(&writer);
        }
    }
#elif defined(ARCH_AARCH64) || defined(ARCH_ARM)

    static const uint32_t plt[] = {
        0x90000010, //adrp x16, .
        0xf9400211, //ldr  x17, [x16, #0]
        /* 0x91000210, */ //add x16, x16, #0
        0xd61f0220  //br x17
    };

    address_t gotPLT = getGotPLTEntry();
    address_t disp = gotPLT - (getAddress() & ~0xFFF);
    uint32_t imm = disp >> 12;

    uint32_t encoding = (imm & 0x3) << 29 | ((imm & 0x1FFFFC) << 3);

    *(uint32_t *)(target + 0) = plt[0] | encoding;

    disp = gotPLT & 0xFFF;
    imm = (disp >> 3) << 10;
    encoding = imm & ~0xFFE003FF;

    *(uint32_t *)(target + 4) = plt[1] | encoding;
    *(uint32_t *)(target + 8) = plt[2];
#endif

    LOG(1, "created PLT entry to " << std::hex << (void *)getGotPLTEntry()
        << " from 0x" << getAddress());
}

address_t PLTTrampoline::getGotPLTEntry() const {
    Module *module = dynamic_cast<Module *>(getParent()->getParent());

    return module->getBaseAddress() + gotPLTEntry;
}

void PLTTrampoline::serialize(ChunkSerializerOperations &op,
    ArchiveStreamWriter &writer) {

    writer.write(getAddress());
    writer.writeID(op.assign(externalSymbol));
    writer.write(gotPLTEntry);
    writer.write<bool>(pltGot);

    op.serializeChildren(this, writer);
}

bool PLTTrampoline::deserialize(ChunkSerializerOperations &op,
    ArchiveStreamReader &reader) {

    setPosition(new AbsolutePosition(reader.read<address_t>()));
    externalSymbol = op.lookupAs<ExternalSymbol>(reader.readID());
    gotPLTEntry = reader.read<address_t>();
    pltGot = reader.read<bool>();

    op.deserializeChildren(this, reader);
    {
        PositionFactory *positionFactory = PositionFactory::getInstance();
        Chunk *prevChunk = this;

        for(uint64_t i = 0; i < getChildren()->genericGetSize(); i ++) {
            auto block = this->getChildren()->getIterable()->get(i);

            if(i > 0) {
                block->setPreviousSibling(prevChunk);
                prevChunk->setNextSibling(block);
            }

            block->setPosition(positionFactory->makePosition(
                prevChunk, block, this->getSize()));
            prevChunk = block;

            this->addToSize(block->getSize());
        }
    }
    ChunkMutator{this, true};  // recalculate addresses
    return reader.stillGood();
}

void PLTTrampoline::accept(ChunkVisitor *visitor) {
    visitor->visit(this);
}

void PLTTrampoline::makeCache() {
    this->cache = new ChunkCache(this);
}

size_t PLTList::getPLTTrampolineSize() {
#ifdef ARCH_X86_64
    return 64;  // must be big enough to hold indirection for JIT-shuffling
#else
    return 16;
#endif
}

void PLTList::serialize(ChunkSerializerOperations &op,
    ArchiveStreamWriter &writer) {

    op.serializeChildren(this, writer);
}

bool PLTList::deserialize(ChunkSerializerOperations &op,
    ArchiveStreamReader &reader) {

    op.deserializeChildren(this, reader);
    return reader.stillGood();
}

void PLTList::accept(ChunkVisitor *visitor) {
    visitor->visit(this);
}

bool PLTList::parsePLTList(ElfMap *elf, RelocList *relocList, Module *module) {
#if 0 // Original parsing logic, fails with newer environments
    auto pltList = parse(relocList, elf, module);
#else // GT specific logic
    auto pltList = gt_parse(relocList, elf, module);
#endif
    if(pltList) {
        module->getChildren()->add(pltList);
        module->setPLTList(pltList);
        pltList->setParent(module);
    }
    return pltList != nullptr;
}

PLTList *PLTList::parse(RelocList *relocList, ElfMap *elf, Module *module) {
    auto pltSection = elf->findSection(".plt");
    if(!pltSection) return nullptr;
    auto header = pltSection->getHeader();
    auto section = elf->getSectionReadPtr<address_t>(pltSection);

#ifdef ARCH_X86_64
    #define R_JUMP_SLOT R_X86_64_JUMP_SLOT
    #define R_IRELATIVE R_X86_64_IRELATIVE
#elif defined(ARCH_AARCH64)
    #define R_JUMP_SLOT R_AARCH64_JUMP_SLOT
    #define R_IRELATIVE R_AARCH64_IRELATIVE
#elif defined(ARCH_RISCV)
    #define R_JUMP_SLOT R_RISCV_JUMP_SLOT
    #define R_IRELATIVE R_RISCV_RELATIVE // XXX: check this
#endif

    PLTRegistry *registry = new PLTRegistry();
    for(auto r : *relocList) {
        if(r->getType() == R_JUMP_SLOT) {
            LOG(1, "PLT entry at " << r->getAddress()
                << " to " << r->getAddend());
            registry->add(r->getAddress(), r);
        }
        else if(r->getType() == R_IRELATIVE) {
            LOG(1, "ifunc PLT entry at " << r->getAddress()
                << " to " << r->getAddend());
            registry->add(r->getAddress(), r);
        }
    }

    PLTList *pltList = new PLTList();

#ifdef ARCH_X86_64
    static const size_t ENTRY_SIZE = 16;

    /* example format
        0000000000000550 <.plt>:
         550:   ff 35 b2 0a 20 00       pushq  0x200ab2(%rip)
         556:   ff 25 b4 0a 20 00       jmpq   *0x200ab4(%rip)
         55c:   0f 1f 40 00             nopl   0x0(%rax)

        0000000000000560 <puts@plt>:
         560:   ff 25 b2 0a 20 00       jmpq   *0x200ab2(%rip)
         566:   68 00 00 00 00          pushq  $0x0
         56b:   e9 e0 ff ff ff          jmpq   550 <.plt>
    */

    // note: we skip the first PLT entry, which has a different format
    for(size_t i = 1 * ENTRY_SIZE; i < header->sh_size; i += ENTRY_SIZE) {
        auto entry = section + i;

        LOG(1, "CONSIDER PLT entry at " << entry);

        if(*reinterpret_cast<const unsigned short *>(entry) == 0x25ff) {
            address_t pltAddress = header->sh_addr + i;
            address_t value = *reinterpret_cast<const unsigned int *>(entry + 2)
                + (pltAddress + 2+4);  // target is RIP-relative
            LOG(1, "PLT value would be " << value);
            Reloc *r = registry->find(value);
            if(r) {
                auto symbol = r->getSymbol();
                if(!symbol) {
                    symbol = module->getElfSpace()->getSymbolList()
                        ->find(r->getAddend());
                }
                auto externalSymbol = ExternalSymbolFactory(module)
                    .makeExternalSymbol(symbol);
                auto trampoline = new PLTTrampoline(
                    pltList, pltAddress, externalSymbol, value);

                static DisasmHandle handle(true);
                auto jmp1 = new Instruction();
                auto jmp1sem = new DataLinkedControlFlowInstruction(X86_INS_JMP, jmp1,
                    "\xff\x25", "jmpq", 4);
                jmp1->setSemantic(jmp1sem);
                /// data link null???
                jmp1sem->setLink(module->getDataRegionList()
                    ->createDataLink(value, module, true));
                LOG(1, "Trying to create link to 0x" << value << ", got "
                        << jmp1sem->getLink());
                //jmp1->setPosition(new AbsolutePosition(0x0));
                //jmp1sem->regenerateAssembly();
                auto push = DisassembleInstruction(handle, true)
                    .instruction(std::string(
                    reinterpret_cast<const char *>(entry + 6), 5));
                auto jmp2 = new Instruction();
                auto jmp2sem = new ControlFlowInstruction(X86_INS_JMP, jmp2,
                    "\xe9", "jmpq", 4);
                jmp2->setSemantic(jmp2sem);

                auto sectionAddr = pltSection->getVirtualAddress();
                jmp2sem->setLink(new UnresolvedLink(sectionAddr));
                LOG(1, "plt section address is " << std::hex << sectionAddr);
                LOG(1, "plt section link is " << std::hex << jmp2sem->getLink());

                auto block = new Block();
                ChunkMutator(trampoline).append(block);
                {
                    ChunkMutator m(block, true);
                    m.append(jmp1);
                    m.append(push);
                    m.append(jmp2);
                }

                pltList->getChildren()->add(trampoline);
            }
        }
    }
#elif defined(ARCH_AARCH64)
    static const size_t ENTRY_SIZE = 16;

    /* example format
        0000000000400420 <puts@plt>:
        400420:       90000090        adrp    x16, 410000 <__FRAME_END__+0xf9c8>
        400424:       f9443611        ldr     x17, [x16,#2152]
        400428:       9121a210        add     x16, x16, #0x868
        40042c:       d61f0220        br      x17
    */

    // note: we skip the first PLT entry, which is 2x the size of others
    for(size_t i = 2 * ENTRY_SIZE; i < header->sh_size; i += ENTRY_SIZE) {
        auto entry = section + i;

        if((*reinterpret_cast<const unsigned char *>(entry+3) & 0x9f) == 0x90) {
            address_t pltAddress = header->sh_addr + i;
            unsigned int bytes = *reinterpret_cast<const unsigned int *>(entry);

            address_t value = ((bytes & 0x60000000) >> 29)  // 2 low-order bits
                | ((bytes & 0xffffe0) >> (5-2));  // 19 high-order bits
            value <<= 12;
            value += (pltAddress) & ~0xfff;  // mask least 12 bits

            unsigned int bytes2 = *reinterpret_cast<const unsigned int *>(entry + 4);

            address_t value2 = ((bytes2 & 0x3ffc00) >> 10) << ((bytes2 & 0xc0000000) >> 30);
            value += value2;

            LOG(1, "VALUE might be " << value);
            Reloc *r = registry->find(value);
            if(r && r->getSymbol()) {
                LOG(1, "Found PLT entry at " << pltAddress << " -> ["
                    << r->getSymbolName() << "]");

                auto externalSymbol = ExternalSymbolFactory(module)
                    .makeExternalSymbol(r->getSymbol());
                pltList->getChildren()->add(
                    new PLTTrampoline(pltList, pltAddress, externalSymbol, value));
            }
        }
    }
#elif defined(ARCH_RISCV)
    static const size_t ENTRY_SIZE = 16;

    /* Example PLT trampoline:
   103b0:       00002e17                auipc   t3,0x2
   103b4:       c60e3e03                ld      t3,-928(t3) # 12010 <puts@GLIBC_2.26>
   103b8:       000e0367                jalr    t1,t3
   103bc:       00000013                nop
     */

    for(size_t i = 2 * ENTRY_SIZE; i < header->sh_size; i += ENTRY_SIZE) {
        auto entry = section + i;
        address_t pltAddress = header->sh_addr + i;

        // start with the 20-bit PC-relative immediate
        address_t base =
            pltAddress + (*reinterpret_cast<int32_t *>(entry) & ~0xfff);
        int32_t offset = *reinterpret_cast<int32_t *>(entry + 4);
        offset >>= 20;

        address_t value = base + offset;
        LOG(1, "VALUE might be " << value);
        Reloc *r = registry->find(value);
        if(r && r->getSymbol()) {
            LOG(1, "Found PLT entry at " << pltAddress << " -> ["
                << r->getSymbolName() << "]");

            auto externalSymbol = ExternalSymbolFactory(module)
                .makeExternalSymbol(r->getSymbol());
            pltList->getChildren()->add(
                new PLTTrampoline(pltList, pltAddress, externalSymbol, value));
        }
    }
#endif

    parsePLTGOT(relocList, elf, pltList, module);
    return pltList;
}

void PLTList::parsePLTGOT(RelocList *relocList, ElfMap *elf,
    PLTList *pltList, Module *module) {

    auto pltgot = elf->findSection(".plt.got");
    if(!pltgot) return;  // no .plt.got section
    auto header = pltgot->getHeader();
    auto section = elf->getSectionReadPtr<address_t>(pltgot);

    PLTRegistry *newRegistry = new PLTRegistry();
    for(auto r : *relocList) {
        if(r->getType() == R_X86_64_GLOB_DAT) {
            LOG(1, "PLT.GOT data at " << r->getAddress());
            newRegistry->add(r->getAddress(), r);
        }
    }

    static const size_t ENTRY_SIZE = 8;

    /* example format
        0x00007ffff7a5b900:  ff 25 3a 85 37 00       jmpq   *0x37853a(%rip)
        0x00007ffff7a5b906:  66 90   xchg   %ax,%ax
    */

    for(size_t i = 0; i < header->sh_size; i += ENTRY_SIZE) {
        auto entry = section + i;

        LOG(1, "CONSIDER PLT.GOT entry at " << entry);

        if(*reinterpret_cast<const unsigned short *>(entry) == 0x25ff) {
            address_t pltAddress = header->sh_addr + i;
            address_t value = *reinterpret_cast<const unsigned int *>(entry + 2)
                + (pltAddress + 2+4);  // target is RIP-relative
            LOG(1, "PLT.GOT value would be " << value);
            Reloc *r = newRegistry->find(value);
            if(r && r->getSymbol()) {
                LOG(1, "Found PLT.GOT entry at " << pltAddress << " -> ["
                    << r->getSymbol()->getName() << "]");
                auto externalSymbol = ExternalSymbolFactory(module)
                    .makeExternalSymbol(r->getSymbol());
                pltList->getChildren()->add(
                    new PLTTrampoline(pltList, pltAddress, externalSymbol, value,
                        true));
            }
        }
    }
}

PLTList *PLTList::gt_parse(RelocList *relocList, ElfMap *elf, Module *module) {
    auto pltSection = elf->findSection(".plt");
    if (!pltSection) return nullptr;
    auto header = pltSection->getHeader();
    auto section = elf->getSectionReadPtr<address_t>(pltSection);

    PLTList *pltList = new PLTList();
    static const size_t ENTRY_SIZE = 16;

#ifdef ARCH_X86_64

    /* example format
        0000000000000550 <.plt>:
         550:   ff 35 b2 0a 20 00       pushq  0x200ab2(%rip)
         556:   ff 25 b4 0a 20 00       jmpq   *0x200ab4(%rip)
         55c:   0f 1f 40 00             nopl   0x0(%rax)

        0000000000000560 <puts@plt>:
         560:   ff 25 b2 0a 20 00       jmpq   *0x200ab2(%rip)
         566:   68 00 00 00 00          pushq  $0x0
         56b:   e9 e0 ff ff ff          jmpq   550 <.plt>
    */
    // note: we skip the first PLT entry, which has a different format
    constexpr size_t plt_section_offset = ENTRY_SIZE;
    constexpr ElfXX_Word r_jump_slot = R_X86_64_JUMP_SLOT;
    constexpr ElfXX_Word r_irelative = R_X86_64_IRELATIVE;
    constexpr ElfXX_Word r_glob_dat = R_X86_64_GLOB_DAT;

#elif defined(ARCH_AARCH64)

    /* example format
        0000000000400420 <puts@plt>:
        400420:       90000090        adrp    x16, 410000 <__FRAME_END__+0xf9c8>
        400424:       f9443611        ldr     x17, [x16,#2152]
        400428:       9121a210        add     x16, x16, #0x868
        40042c:       d61f0220        br      x17
    */
    // note: we skip the first PLT entry, which is 2x the size of others
    constexpr size_t plt_section_offset = ENTRY_SIZE * 2;
    constexpr ElfXX_Word r_jump_slot = R_AARCH64_JUMP_SLOT;
    constexpr ElfXX_Word r_irelative = R_AARCH64_IRELATIVE;
    constexpr ElfXX_Word r_glob_dat = R_AARCH64_GLOB_DAT;

#elif defined(ARCH_RISCV)

    /* Example PLT trampoline:
   103b0:       00002e17                auipc   t3,0x2
   103b4:       c60e3e03                ld      t3,-928(t3) # 12010
   <puts@GLIBC_2.26> 103b8:       000e0367                jalr    t1,t3 103bc:
   00000013                nop
     */
    // note: we skip the first PLT entry, which is 2x the size of others
    constexpr size_t plt_section_offset = ENTRY_SIZE * 2;
    constexpr ElfXX_Word r_jump_slot = R_RISCV_JUMP_SLOT;
    constexpr ElfXX_Word r_irelative = R_RISCV_RELATIVE;  // XXX: check this
    constexpr ElfXX_Word r_glob_dat =
        R_RISCV_GOT_HI20;  // XXX: check this too, may need to be be R_RISCV_64

#endif

    PLTRegistry *registry = new PLTRegistry();
    for (auto r : *relocList) {
        if (r->getType() == r_jump_slot) {
            LOG(1,
                "PLT entry at " << r->getAddress() << " to " << r->getAddend());
            registry->add(r->getAddress(), r);
        }
        else if (r->getType() == r_irelative) {
            LOG(1, "ifunc PLT entry at " << r->getAddress() << " to "
                                         << r->getAddend());
            registry->add(r->getAddress(), r);
        }
        else if (r->getType() == r_glob_dat) {
            LOG(1, "PLT.GOT data at " << r->getAddress());
            registry->add(r->getAddress(), r);
        }
    }

    parsePLTLazy(
        ".plt", plt_section_offset, ENTRY_SIZE, registry, elf, pltList, module);
    parsePLTNonLazy(".plt.got", 8, registry, elf, pltList, module);
    parsePLTNonLazy(".plt.sec", 8, registry, elf, pltList, module);
    parsePLTNonLazy(".plt.bnd", 8, registry, elf, pltList, module);
    return pltList;
}

void PLTList::parsePLTLazy(const char *sectionName, const size_t sectionOffset,
    const size_t entrySize, PLTRegistry *registry, ElfMap *elf,
    PLTList *pltList, Module *module) {
    auto pltSection = elf->findSection(sectionName);
    if (!pltSection) return;  // no section named sectionName
    auto header = pltSection->getHeader();
    auto section = elf->getSectionReadPtr<address_t>(pltSection);

    for (size_t i = sectionOffset; i < header->sh_size; i += entrySize) {
        auto entry = section + i;
        LOG(1, "CONSIDER PLT entry at " << entry);

        address_t pltAddress = header->sh_addr + i;
        parseEntryLazy(
            pltSection, entry, header, pltAddress, registry, pltList, module);
    }
}

#ifdef ARCH_X86_64

void PLTList::parseEntryLazy(ElfSection *pltSection, address_t entry,
    ElfXX_Shdr *header, address_t pltAddress, PLTRegistry *registry,
    PLTList *pltList, Module *module) {
    // Make sure we skip initial endbr64 instruction for GCC-8+ builds
    address_t valueOffset = 6;  // target is RIP-relative
    if (*reinterpret_cast<const unsigned int *>(entry) == 0xfa1e0ff3) {
        entry += 4;
        valueOffset += 4;
    }
    // Handle BND JMPQ
    if (*reinterpret_cast<const uint8_t *>(entry) == 0xf2) {
        entry += 1;
        valueOffset += 1;
    }

    if (*reinterpret_cast<const unsigned short *>(entry) == 0x25ff) {
        address_t value = *reinterpret_cast<const unsigned int *>(entry + 2) +
                          (pltAddress + valueOffset);
        LOG(1, "PLT value would be " << value);
        Reloc *r = registry->find(value);
        if (r) {
            auto symbol = r->getSymbol();
            if (!symbol) {
                symbol = module->getElfSpace()->getSymbolList()->find(
                    r->getAddend());
            }
            auto externalSymbol = ExternalSymbolFactory(module)
                                      .makeExternalSymbol(symbol);
            auto trampoline = new PLTTrampoline(
                pltList, pltAddress, externalSymbol, value);

            static DisasmHandle handle(true);
            auto jmp1 = new Instruction();
            auto jmp1sem = new DataLinkedControlFlowInstruction(
                X86_INS_JMP, jmp1, "\xff\x25", "jmpq", 4);
            jmp1->setSemantic(jmp1sem);
            /// data link null???
            jmp1sem->setLink(module->getDataRegionList()->createDataLink(
                value, module, true));
            LOG(1, "Trying to create link to 0x" << value << ", got "
                                                 << jmp1sem->getLink());
            // jmp1->setPosition(new AbsolutePosition(0x0));
            // jmp1sem->regenerateAssembly();
            auto push = DisassembleInstruction(handle, true)
                            .instruction(std::string(
                                reinterpret_cast<const char *>(entry + 6), 5));
            auto jmp2 = new Instruction();
            auto jmp2sem = new ControlFlowInstruction(
                X86_INS_JMP, jmp2, "\xe9", "jmpq", 4);
            jmp2->setSemantic(jmp2sem);

            auto sectionAddr = pltSection->getVirtualAddress();
            jmp2sem->setLink(new UnresolvedLink(sectionAddr));
            LOG(1, "plt section address is " << std::hex << sectionAddr);
            LOG(1, "plt section link is " << std::hex << jmp2sem->getLink());

            auto block = new Block();
            ChunkMutator(trampoline).append(block);
            {
                ChunkMutator m(block, true);
                m.append(jmp1);
                m.append(push);
                m.append(jmp2);
            }

            pltList->getChildren()->add(trampoline);
        }
    }
}

#elif defined(ARCH_AARCH64)

void PLTList::parseEntryLazy(ElfSection *pltSection, address_t entry,
    ElfXX_Shdr *header, address_t pltAddress, PLTRegistry *registry,
    PLTList *pltList, Module *module) {
    if ((*reinterpret_cast<const unsigned char *>(entry + 3) & 0x9f) == 0x90) {
        unsigned int bytes = *reinterpret_cast<const unsigned int *>(entry);

        address_t value = ((bytes & 0x60000000) >> 29)  // 2 low-order bits
                          | ((bytes & 0xffffe0) >>
                                (5 - 2));  // 19 high-order bits
        value <<= 12;
        value += (pltAddress) & ~0xfff;  // mask least 12 bits

        unsigned int bytes2 = *reinterpret_cast<const unsigned int *>(
            entry + 4);

        address_t value2 = ((bytes2 & 0x3ffc00) >> 10)
                           << ((bytes2 & 0xc0000000) >> 30);
        value += value2;

        LOG(1, "VALUE might be " << value);
        Reloc *r = registry->find(value);
        if (r && r->getSymbol()) {
            LOG(1, "Found PLT entry at " << pltAddress << " -> ["
                                         << r->getSymbolName() << "]");

            auto externalSymbol = ExternalSymbolFactory(module)
                                      .makeExternalSymbol(r->getSymbol());
            pltList->getChildren()->add(
                new PLTTrampoline(pltList, pltAddress, externalSymbol, value));
        }
    }
}

#elif defined(ARCH_RISCV)

void PLTList::parseEntryLazy(ElfSection *pltSection, address_t entry,
    ElfXX_Shdr *header, address_t pltAddress, PLTRegistry *registry,
    PLTList *pltList, Module *module) {
    // start with the 20-bit PC-relative immediate
    address_t base = pltAddress +
                     (*reinterpret_cast<int32_t *>(entry) & ~0xfff);
    int32_t offset = *reinterpret_cast<int32_t *>(entry + 4);
    offset >>= 20;

    address_t value = base + offset;
    LOG(1, "VALUE might be " << value);
    Reloc *r = registry->find(value);
    if (r && r->getSymbol()) {
        LOG(1, "Found PLT entry at " << pltAddress << " -> ["
                                     << r->getSymbolName() << "]");

        auto externalSymbol = ExternalSymbolFactory(module).makeExternalSymbol(
            r->getSymbol());
        pltList->getChildren()->add(
            new PLTTrampoline(pltList, pltAddress, externalSymbol, value));
    }
}

#endif

void PLTList::parsePLTNonLazy(const char *sectionName, const size_t entrySize,
    PLTRegistry *registry, ElfMap *elf, PLTList *pltList, Module *module) {
    auto pltSection = elf->findSection(sectionName);
    if (!pltSection) return;  // no section named sectionName
    auto header = pltSection->getHeader();
    auto section = elf->getSectionReadPtr<address_t>(pltSection);

    for (size_t i = 0; i < header->sh_size; i += entrySize) {
        auto entry = section + i;

        LOG(1, "CONSIDER " << sectionName << " entry at " << entry);

        // Make sure we skip initial endbr64 instruction for GCC-8+ builds
        address_t valueOffset = 6;  // target is RIP-relative
        if (*reinterpret_cast<const unsigned int *>(entry) == 0xfa1e0ff3) {
            entry += 4;
            valueOffset += 4;
        }
        // Handle BND JMPQ
        if (*reinterpret_cast<const uint8_t *>(entry) == 0xf2) {
            entry += 1;
            valueOffset += 1;
        }

        if (*reinterpret_cast<const unsigned short *>(entry) == 0x25ff) {
            address_t pltAddress = header->sh_addr + i;
            address_t value = *reinterpret_cast<const unsigned int *>(
                                  entry + 2) +
                              (pltAddress + valueOffset);
            LOG(1, sectionName << " value would be " << value);
            Reloc *r = registry->find(value);
            if (r && r->getSymbol()) {
                LOG(1, "Found " << sectionName << " entry at " << pltAddress
                                << " -> [" << r->getSymbol()->getName() << "]");
                auto externalSymbol = ExternalSymbolFactory(module)
                                          .makeExternalSymbol(r->getSymbol());
                pltList->getChildren()->add(new PLTTrampoline(
                    pltList, pltAddress, externalSymbol, value, true));
            }
        }
    }
}
