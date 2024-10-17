#include <JIT.h>
#include <string.h>
#include <sys/mman.h>

void JIT::mov64(Operand dst, Operand src)
{
    if (dst.type == Operand::Type::Register && src.type == Operand::Type::Immediate)
    {
        rex(dst, true);
        write8(0xb8 + encode_register(dst.reg)); // mov r64, imm64
        write64(src.immediate);
        return;
    }

    if (dst.is_register_or_memory() && src.type == Operand::Type::Register)
    {
        if (dst.type == Operand::Type::Register && dst.reg == src.reg)
            return;

        rex_rm(src, dst, true);
        write8(0x89); // mov r/m64, r64
        mod_rm_register(src, dst);
        return;
    }

    if (dst.type == Operand::Type::Register && src.is_register_or_memory())
    {
        rex_rm(dst, src, true);
        write8(0x8b); // mov r64, r/m64
        mod_rm_register(dst, src);
        return;
    }

    assert(false);
}

void JIT::mov32(Operand dst, Operand src)
{
    if (dst.is_register_or_memory() && src.type == Operand::Type::Register)
    {
        if (dst.type == Operand::Type::Register && dst.reg == src.reg)
            return;

        rex_rm(src, dst, false);
        write8(0x89); // mov r/m32, r32
        mod_rm_register(src, dst);
        return;
    }

    if (dst.type == Operand::Type::Register && src.is_register_or_memory())
    {
        rex_rm(dst, src, false);
        write8(0x8b); // mov r32, r/m32
        mod_rm_register(dst, src);
        return;
    }

    assert(false);
}

void JIT::add64(Operand dst, Operand src)
{
    if (dst.type == Operand::Type::Register && src.type == Operand::Type::Register)
    {
        rex_rm(dst, src, true);
        write8(0x03); // add r64, r/m64
        mod_rm_register(dst, src);
        return;
    }

    assert(false);
}

void JIT::add32(Operand dst, Operand src)
{
    if (dst.type == Operand::Type::Register && src.type == Operand::Type::Register)
    {
        rex_rm(dst, src, false);
        write8(0x03); // add r32, r/m32
        mod_rm_register(dst, src);
        return;
    }

    assert(false);
}

void JIT::exit()
{
    write8(0xc3); // ret
}

void JIT::push64(Operand arg)
{
    if (arg.type == Operand::Type::Register)
    {
        rex(arg, true);
        write8(0x50 + encode_register(arg.reg)); // push r64
        return;
    }

    assert(false);
}

void JIT::pop64(Operand arg)
{
    if (arg.type == Operand::Type::Register)
    {
        rex(arg, true);
        write8(0x58 + encode_register(arg.reg)); // pop r64
        return;
    }

    assert(false);
}

void JIT::native_call(void* callee)
{
    // push caller-saved registers on the stack
    // (callee-saved registers: RBX, RSP, RBP, and R12–R15)

    // push RCX, RDX, RSI, RDI, R8, R9, R10, R11
    write8(0x51);
    write8(0x52);
    write8(0x56);
    write8(0x57);
    write8(0x41);
    write8(0x50);
    write8(0x41);
    write8(0x51);
    write8(0x41);
    write8(0x52);
    write8(0x41);
    write8(0x53);

    // align the stack to 16-byte boundary
    write8(0x48);
    write8(0x83);
    write8(0xec);
    write8(0x08);

    // load callee into RAX and make indirect call
    write8(0x48);
    write8(0xb8);
    write64((uint64_t)callee);
    write8(0xff);
    write8(0xd0);

    // adjust stack pointer
    write8(0x48);
    write8(0x83);
    write8(0xc4);
    write8(0x08);

    // restore caller-saved registers from the stack
    // pop R11, R10, R9, R8, RDI, RSI, RDX, RCX
    write8(0x41);
    write8(0x5b);
    write8(0x41);
    write8(0x5a);
    write8(0x41);
    write8(0x59);
    write8(0x41);
    write8(0x58);
    write8(0x5f);
    write8(0x5e);
    write8(0x5a);
    write8(0x59);
}

void* JIT::build()
{
    FILE* f = fopen("jit.bin", "wb");
    fwrite(code.data(), code.size(), 1, f);
    fclose(f);

    void* memory = mmap(nullptr, code.size(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy(memory, code.data(), code.size());
    mprotect(memory, code.size(), PROT_READ | PROT_EXEC);
    return memory;
}

void JIT::write8(uint8_t data)
{
    code.push_back(data);
}

void JIT::write16(uint16_t data)
{
    code.push_back((data >> 0) & 0xFF);
    code.push_back((data >> 8) & 0xFF);
}

void JIT::write32(uint32_t data)
{
    code.push_back((data >> 0) & 0xFF);
    code.push_back((data >> 8) & 0xFF);
    code.push_back((data >> 16) & 0xFF);
    code.push_back((data >> 24) & 0xFF);
}

void JIT::write64(uint64_t data)
{
    code.push_back((data >> 0) & 0xFF);
    code.push_back((data >> 8) & 0xFF);
    code.push_back((data >> 16) & 0xFF);
    code.push_back((data >> 24) & 0xFF);
    code.push_back((data >> 32) & 0xFF);
    code.push_back((data >> 40) & 0xFF);
    code.push_back((data >> 48) & 0xFF);
    code.push_back((data >> 56) & 0xFF);
}

void JIT::rex(Operand arg, bool W)
{
    assert(arg.is_register_or_memory());

    REX r {};
    r.B = static_cast<uint8_t>(arg.reg) >= 8;
    r.W = W;
    write8(std::bit_cast<uint8_t>(r));
}

void JIT::rex_rm(Operand reg, Operand rm, bool W)
{
    assert(rm.is_register_or_memory());
    assert(reg.type == Operand::Type::Register);

    REX r {};
    r.B = static_cast<uint8_t>(rm.reg) >= 8;
    r.R = static_cast<uint8_t>(reg.reg) >= 8;
    r.W = W;
    write8(std::bit_cast<uint8_t>(r));
}

void JIT::mod_rm_register(Operand reg, Operand rm)
{
    assert(reg.type == Operand::Type::Register && rm.is_register_or_memory());

    ModRM raw;
    raw.reg = encode_register(reg.reg);
    raw.rm = encode_register(rm.reg);

    if (rm.type == Operand::Type::Register)
    {
        raw.mod = ModRM::Register;
        write8(raw.raw);
    }
    else if (rm.type == Operand::Type::MemoryBaseAndOffset)
    {
        // FIXME: Emit smaller elements if the displacement is small
        raw.mod = ModRM::MemoryDisplacent32;
        write8(raw.raw);
        write32(rm.immediate);
    }
    else
    {
        assert(false);
    }
}
