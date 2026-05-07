#include "reduct/compile.h"
#include "reduct/disasm.h"

static void reduct_disasm_internal(reduct_t* reduct, reduct_function_t* function, FILE* out)
{
    assert(reduct != NULL);
    assert(function != NULL);
    assert(out != NULL);

    if (reduct == NULL || function == NULL || out == NULL)
    {
        return;
    }

    fprintf(out, "================================================================================\n");
    fprintf(out, "Function: %p\n", (void*)function);
    fprintf(out, "Arity: %u\n", (unsigned int)function->arity);
    fprintf(out, "Instruction count: %u\n", (unsigned int)function->instCount);
    fprintf(out, "Constant count: %u\n", (unsigned int)function->constantCount);
    fprintf(out, "--------------------------------------------------------------------------------\n");

    for (size_t i = 0; i < function->instCount; ++i)
    {
        reduct_inst_t inst = function->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP_BASE(inst);
        reduct_bool_t isConst = (REDUCT_INST_GET_OP(inst) & REDUCT_MODE_CONST) != 0;
        uint32_t a = REDUCT_INST_GET_A(inst);
        uint32_t b = REDUCT_INST_GET_B(inst);
        uint32_t c = REDUCT_INST_GET_C(inst);
        uint32_t sbx = REDUCT_INST_GET_SBX(inst);

        const char* opName = "UNKNOWN";
        switch (op)
        {
        case REDUCT_OPCODE_LIST:
            opName = "LIST";
            break;
        case REDUCT_OPCODE_JMP:
            opName = "JMP";
            break;
        case REDUCT_OPCODE_JMPF:
            opName = "JMPF";
            break;
        case REDUCT_OPCODE_JMPT:
            opName = "JMPT";
            break;
        case REDUCT_OPCODE_CALL:
            opName = "CALL";
            break;
        case REDUCT_OPCODE_RET:
            opName = "RET";
            break;
        case REDUCT_OPCODE_MOV:
            opName = "MOV";
            break;
        case REDUCT_OPCODE_EQ:
            opName = "EQ";
            break;
        case REDUCT_OPCODE_NEQ:
            opName = "NEQ";
            break;
        case REDUCT_OPCODE_SEQ:
            opName = "SEQ";
            break;
        case REDUCT_OPCODE_SNEQ:
            opName = "SNEQ";
            break;
        case REDUCT_OPCODE_LT:
            opName = "LT";
            break;
        case REDUCT_OPCODE_LE:
            opName = "LE";
            break;
        case REDUCT_OPCODE_GT:
            opName = "GT";
            break;
        case REDUCT_OPCODE_GE:
            opName = "GE";
            break;
        case REDUCT_OPCODE_ADD:
            opName = "ADD";
            break;
        case REDUCT_OPCODE_SUB:
            opName = "SUB";
            break;
        case REDUCT_OPCODE_MUL:
            opName = "MUL";
            break;
        case REDUCT_OPCODE_DIV:
            opName = "DIV";
            break;
        case REDUCT_OPCODE_MOD:
            opName = "MOD";
            break;
        case REDUCT_OPCODE_BAND:
            opName = "BAND";
            break;
        case REDUCT_OPCODE_BOR:
            opName = "BOR";
            break;
        case REDUCT_OPCODE_BXOR:
            opName = "BXOR";
            break;
        case REDUCT_OPCODE_BNOT:
            opName = "BNOT";
            break;
        case REDUCT_OPCODE_SHL:
            opName = "SHL";
            break;
        case REDUCT_OPCODE_SHR:
            opName = "SHR";
            break;
        case REDUCT_OPCODE_CLOSURE:
            opName = "CLOSURE";
            break;
        case REDUCT_OPCODE_CAPTURE:
            opName = "CAPTURE";
            break;
        case REDUCT_OPCODE_TAILCALL:
            opName = "TAILCALL";
            break;
        default:
            break;
        }

        fprintf(out, "[%04u] %-12s ", (unsigned int)i, opName);

        switch (REDUCT_INST_GET_OP_BASE(inst))
        {
        case REDUCT_OPCODE_LIST:
            fprintf(out, "R%-5u %-6u", a, b);
            break;
        case REDUCT_OPCODE_JMP:
            fprintf(out, "%-6d %-6s %-6s", sbx, "", "");
            break;
        case REDUCT_OPCODE_JMPF:
        case REDUCT_OPCODE_JMPT:
            fprintf(out, "R%-5u %-6d %-6s", a, sbx, "");
            break;
        case REDUCT_OPCODE_TAILCALL:
        case REDUCT_OPCODE_CALL:
        case REDUCT_OPCODE_CAPTURE:
            fprintf(out, "R%-5u %-6u %c%-5u", a, b, isConst ? 'K' : 'R', c);
            break;
        case REDUCT_OPCODE_RET:
            fprintf(out, "%c%-5u %-6s %-6s", isConst ? 'K' : 'R', c, "", "");
            break;
        case REDUCT_OPCODE_MOV:
        case REDUCT_OPCODE_BNOT:
            fprintf(out, "R%-5u %-6s %c%-5u", a, "", isConst ? 'K' : 'R', c);
            break;
        case REDUCT_OPCODE_CLOSURE:
            fprintf(out, "R%-5u %-6s K%-5u", a, "", c);
            break;
        default:
            fprintf(out, "R%-5u R%-5u %c%-5u", a, b, isConst ? 'K' : 'R', c);
            break;
        }

        reduct_bool_t hasInlineConst = REDUCT_FALSE;
        if (op == REDUCT_OPCODE_CLOSURE || isConst)
        {
            hasInlineConst = REDUCT_TRUE;
        }

        if (op == REDUCT_OPCODE_JMP || op == REDUCT_OPCODE_JMPF || op == REDUCT_OPCODE_JMPT)
        {
            int target = (int)i + 1 + sbx;
            fprintf(out, " ; -> [%04u]\n", target);
        }
        else if (hasInlineConst && c < function->constantCount)
        {
            reduct_const_slot_t* slot = &function->constants[c];
            fprintf(out, " ; ");
            if (slot->type == REDUCT_CONST_SLOT_ITEM)
            {
                reduct_item_t* item = slot->item;
                if (item->type == REDUCT_ITEM_TYPE_ATOM)
                {
                    fprintf(out, "\"%.*s\"\n", (int)item->atom.length, item->atom.string);
                }
                else if (item->type == REDUCT_ITEM_TYPE_LIST)
                {
                    fprintf(out, "(list of %u handles)\n", (unsigned int)item->list.length);
                }
                else if (item->type == REDUCT_ITEM_TYPE_FUNCTION)
                {
                    fprintf(out, "(function %p)\n", (void*)&item->function);
                }
                else
                {
                    fprintf(out, "(unknown item)\n");
                }
            }
            else if (slot->type == REDUCT_CONST_SLOT_CAPTURE)
            {
                fprintf(out, "(capture %.*s)\n", (int)slot->capture->length, slot->capture->string);
            }
            else
            {
                fprintf(out, "(none)\n");
            }
        }
        else
        {
            fprintf(out, "\n");
        }
    }

    if (function->constantCount > 0)
    {
        fprintf(out, "--------------------------------------------------------------------------------\n");
        for (uint16_t i = 0; i < function->constantCount; ++i)
        {
            reduct_const_slot_t* slot = &function->constants[i];
            if (slot->type == REDUCT_CONST_SLOT_ITEM)
            {
                reduct_item_t* item = slot->item;
                if (item->type == REDUCT_ITEM_TYPE_ATOM)
                {
                    fprintf(out, "[K%03u] \"%.*s\"\n", (unsigned int)i, (int)item->atom.length, item->atom.string);
                }
                else if (item->type == REDUCT_ITEM_TYPE_LIST)
                {
                    fprintf(out, "[K%03u] (list of %u handles)\n", (unsigned int)i,
                        (unsigned int)item->list.length);
                }
                else if (item->type == REDUCT_ITEM_TYPE_FUNCTION)
                {
                    fprintf(out, "[K%03u] (function %p)\n", (unsigned int)i, (void*)&item->function);
                }
                else
                {
                    fprintf(out, "[K%03u] (unknown type)\n", (unsigned int)i);
                }
            }
            else if (slot->type == REDUCT_CONST_SLOT_CAPTURE)
            {
                fprintf(out, "[K%03u] (capture %.*s)\n", (unsigned int)i, (int)slot->capture->length, slot->capture->string);
            }
            else
            {
                fprintf(out, "[K%03u] (none)\n", (unsigned int)i);
            }
        }
    }

    fprintf(out, "================================================================================\n");

    for (uint16_t i = 0; i < function->constantCount; ++i)
    {
        reduct_const_slot_t* slot = &function->constants[i];
        if (slot->type == REDUCT_CONST_SLOT_ITEM)
        {
            reduct_item_t* item = slot->item;
            if (item->type == REDUCT_ITEM_TYPE_FUNCTION)
            {
                reduct_disasm_internal(reduct, &item->function, out);
            }
        }
    }
}

REDUCT_API void reduct_disasm(reduct_t* reduct, reduct_function_t* function, FILE* out)
{
    assert(reduct != NULL);
    assert(function != NULL);
    assert(out != NULL);

    reduct_disasm_internal(reduct, function, out);
}