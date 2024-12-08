#include <JIT.h>
#include <cassert>
#include <cstring>
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

        rex_rm(dst, src, true);
        write8(0x89); // mov r/m64, r64
        mod_rm_register(dst, src);
        return;
    }

    if (dst.type == Operand::Type::Register && src.is_register_or_memory())
    {
        rex_rm(src, dst, true);
        write8(0x8b); // mov r64, r/m64
        mod_rm_register(src, dst);
        return;
    }

    assert(false);
}

void JIT::mov32(Operand dst, Operand src)
{
    if (dst.type == Operand::Type::Register && src.type == Operand::Type::Immediate)
    {
        rex(dst, false);
        write8(0xb8 + encode_register(dst.reg)); // mov r32, imm32
        write32(src.immediate);
        return;
    }

    if (dst.is_register_or_memory() && src.type == Operand::Type::Register)
    {
        if (dst.type == Operand::Type::Register && dst.reg == src.reg)
            return;

        rex_rm(dst, src, false);
        write8(0x89); // mov r/m32, r32
        mod_rm_register(dst, src);
        return;
    }

    if (dst.type == Operand::Type::Register && src.is_register_or_memory())
    {
        rex_rm(src, dst, false);
        write8(0x8b); // mov r32, r/m32
        mod_rm_register(src, dst);
        return;
    }

    assert(false);
}

void JIT::add64(Operand dst, Operand src)
{
    if (dst.type == Operand::Type::Register && src.type == Operand::Type::Register)
    {
        rex_rm(src, dst, true);
        write8(0x03); // add r64, r/m64
        mod_rm_register(src, dst);
        return;
    }

    assert(false);
}

void JIT::add32(Operand dst, Operand src)
{
    if (dst.type == Operand::Type::Register && src.type == Operand::Type::Register)
    {
        rex_rm(src, dst, false);
        write8(0x03); // add r32, r/m32
        mod_rm_register(src, dst);
        return;
    }

    assert(false);
}

void JIT::sub64(Operand dst, Operand src)
{
    if (dst.type == Operand::Type::Register && src.type == Operand::Type::Register)
    {
        rex_rm(src, dst, true);
        write8(0x2B); // sub r64, r/m64
        mod_rm_register(src, dst);
        return;
    }
    else if (dst.is_register_or_memory() && src.type == Operand::Type::Immediate)
    {
        rex(dst, true);
        write8(0x81); // sub r/m64, imm64
        mod_rm(dst, 5);
        assert(src.immediate <= UINT32_MAX);
        write32(src.immediate);
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

void JIT::nop()
{
    write8(0x90);
}

void JIT::native_call(void* callee)
{
    // push caller-saved registers on the stack
    // (callee-saved registers: RBX, RSP, RBP, and R12–R15)

    // push RCX, RDX, RSI, RDI, R8, R9, R10, R11
    push64(JIT::Operand::Register(JIT::Reg::RCX));
    push64(JIT::Operand::Register(JIT::Reg::RDX));
    push64(JIT::Operand::Register(JIT::Reg::RSI));
    push64(JIT::Operand::Register(JIT::Reg::RDI));
    push64(JIT::Operand::Register(JIT::Reg::R8));
    push64(JIT::Operand::Register(JIT::Reg::R9));
    push64(JIT::Operand::Register(JIT::Reg::R10));
    push64(JIT::Operand::Register(JIT::Reg::R11));

    // align the stack to 16-byte boundary
    write8(0x48);
    write8(0x83);
    write8(0xec);
    write8(0x08); // sub rsp, 0x8

    // load callee into RAX and make indirect call
    mov64(JIT::Operand::Register(JIT::Reg::RAX), JIT::Operand::Immediate((uint64_t)callee));
    write8(0xff);
    write8(0xd0); // call rax

    // adjust stack pointer
    write8(0x48);
    write8(0x83);
    write8(0xc4);
    write8(0x08); // add rsp, 0x8

    // restore caller-saved registers from the stack
    // pop R11, R10, R9, R8, RDI, RSI, RDX, RCX
    pop64(JIT::Operand::Register(JIT::Reg::R11));
    pop64(JIT::Operand::Register(JIT::Reg::R10));
    pop64(JIT::Operand::Register(JIT::Reg::R9));
    pop64(JIT::Operand::Register(JIT::Reg::R8));
    pop64(JIT::Operand::Register(JIT::Reg::RDI));
    pop64(JIT::Operand::Register(JIT::Reg::RSI));
    pop64(JIT::Operand::Register(JIT::Reg::RDX));
    pop64(JIT::Operand::Register(JIT::Reg::RCX));
}

void* JIT::build()
{
    FILE* f = fopen("/home/undefine/Coding/WASVM/jit.bin", "wb");
    assert(f);
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

void JIT::rex_rm(Operand rm, Operand reg, bool W)
{
    assert(rm.is_register_or_memory());
    assert(reg.type == Operand::Type::Register);

    REX r {};
    r.B = static_cast<uint8_t>(rm.reg) >= 8;
    r.R = static_cast<uint8_t>(reg.reg) >= 8;
    r.W = W;
    write8(std::bit_cast<uint8_t>(r));
}

void JIT::mod_rm_register(Operand rm, Operand reg)
{
    assert(reg.type == Operand::Type::Register && rm.is_register_or_memory());

    mod_rm(rm, encode_register(reg.reg));
}

void JIT::mod_rm(Operand rm, uint8_t reg)
{
    assert(rm.is_register_or_memory());

    ModRM raw;
    raw.reg = reg;
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
