#include "stackxor.h"
#include "chunk/disassemble.h"

void StackXOR::visit(Function *function) {
    addInstructions(function);
    recurse(function);
}

void StackXOR::visit(Block *block) {
    recurse(block);
}

void StackXOR::visit(Instruction *instruction) {

}

void StackXOR::addInstructions(Function *chunk) {
    /*   
        0000000000000000 <xor_ret_addr>:
           0:   64 4c 8b 1c 25 28 00    mov    %fs:0x28,%r11
           7:   00 00
           9:   4c 31 1c 24             xor    %r11,(%rsp)
    */

    auto first = chunk->getChildren()->getIterable()->get(0);
    auto list = first->getChildren()->getIterable();
    insertAt(first, 0, Disassemble::instruction(
        {0x90}));
    insertAt(first, 1, Disassemble::instruction(
        {0x90}));
    insertAt(first, 2, Disassemble::instruction(
        {0x64, 0x4c, 0x8b, 0x1c, 0x25, xorOffset, 0x00, 0x00, 0x00}));
    insertAt(first, 3, Disassemble::instruction(
        {0x4c, 0x31, 0x1c, 0x24}));
}

void StackXOR::insertAt(Block *block, int index, Instruction *instr) {
    auto list = block->getChildren()->getIterable();
    if(index == 0) {
        instr->setPosition(new RelativePosition(instr, 0));
    }
    else {
        instr->setPosition(
            new SubsequentPosition(list->get(index - 1)));

    }

    if(index < block->getChildren()->getIterable()->getCount()) {
        list->get(index)->setPosition(new SubsequentPosition(instr));
    }

    list->insertAt(index, instr);
    instr->setParent(block);
    block->addToSize(instr->getSize());
}
