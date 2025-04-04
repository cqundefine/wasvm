#pragma once

#include <Util.h>

class JIT
{
public:
    enum class Reg : uint8_t
    {
        RAX = 0,
        RCX = 1,
        RDX = 2,
        RBX = 3,
        RSP = 4,
        RBP = 5,
        RSI = 6,
        RDI = 7,
        R8 = 8,
        R9 = 9,
        R10 = 10,
        R11 = 11,
        R12 = 12,
        R13 = 13,
        R14 = 14,
        R15 = 15,
    };

    struct Operand
    {
        enum class Type
        {
            Register,
            Immediate,
            MemoryBaseAndOffset
        };

        Type type;

        Reg reg;
        uint64_t immediate;

        static Operand Register(Reg reg)
        {
            Operand operand;
            operand.type = Type::Register;
            operand.reg = reg;
            operand.immediate = -1;
            return operand;
        }

        static Operand Immediate(uint64_t immediate)
        {
            Operand operand;
            operand.type = Type::Immediate;
            operand.reg = {};
            operand.immediate = immediate;
            return operand;
        }

        static Operand MemoryBaseAndOffset(Reg base, uint64_t offset)
        {
            Operand operand;
            operand.type = Type::MemoryBaseAndOffset;
            operand.reg = base;
            operand.immediate = offset;
            return operand;
        }

        inline bool is_register_or_memory()
        {
            return type == Type::Register || type == Type::MemoryBaseAndOffset;
        }
    };

    struct Label
    {
        size_t offset_of_label_in_instruction_stream { 0 };
        std::vector<size_t> jump_slot_offsets_in_instruction_stream;

        void add_jump(size_t offset)
        {
            jump_slot_offsets_in_instruction_stream.push_back(offset);
        }

        void link(JIT& jit)
        {
            for (auto offset_in_instruction_stream : jump_slot_offsets_in_instruction_stream)
            {
                auto offset = offset_of_label_in_instruction_stream - offset_in_instruction_stream;
                auto jump_slot = offset_in_instruction_stream - 4;
                jit.code[jump_slot + 0] = (offset >> 0) & 0xff;
                jit.code[jump_slot + 1] = (offset >> 8) & 0xff;
                jit.code[jump_slot + 2] = (offset >> 16) & 0xff;
                jit.code[jump_slot + 3] = (offset >> 24) & 0xff;
            }
        }
    };

    friend Label;

    void mov64(Operand dst, Operand src);
    void mov32(Operand dst, Operand src);

    void add64(Operand dst, Operand src);
    void add32(Operand dst, Operand src);

    void sub64(Operand dst, Operand src);

    void mul64(Operand dst, Operand src);

    void begin();
    void exit();

    void push64(Operand arg);
    void pop64(Operand arg);

    void nop();

    [[nodiscard]] Label make_label();
    void jump(Label& label);
    void update_label(Label& label);

    void native_call(void* callee);

    void* build();

private:
    void write8(uint8_t data);
    void write16(uint16_t data);
    void write32(uint32_t data);
    void write64(uint64_t data);

    constexpr uint8_t encode_register(Reg reg)
    {
        return static_cast<uint8_t>(reg) & 0b111;
    }

    union REX
    {
        struct
        {
            uint8_t B : 1; // ModRM::RM
            uint8_t X : 1; // SIB::Index
            uint8_t R : 1; // ModRM::Reg
            uint8_t W : 1; // Operand size override
            uint8_t _ : 4 { 0b0100 };
        };
        uint8_t raw;
    };

    void rex(Operand arg, bool W);
    void rex_rm(Operand rm, Operand reg, bool W);

    union ModRM
    {
        static constexpr uint8_t Memory = 0b00;
        static constexpr uint8_t MemoryDisplacent8 = 0b01;
        static constexpr uint8_t MemoryDisplacent32 = 0b10;
        static constexpr uint8_t Register = 0b11;
        struct
        {
            uint8_t rm : 3;
            uint8_t reg : 3;
            uint8_t mod : 2;
        };
        uint8_t raw;
    };
    static_assert(sizeof(ModRM) == 1);

    void mod_rm_register(Operand rm, Operand reg);
    void mod_rm(Operand rm, uint8_t reg);

    std::vector<uint8_t> code;
};
