#include "reduct/disasm.h"
#include "reduct/atom.h"
#include "reduct/compile.h"
#include "reduct/item.h"

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
        reduct_opcode_t op = REDUCT_INST_GET_OP(inst);
        bool isConst = (op & REDUCT_MODE_CONST) != 0;
        uint32_t a = REDUCT_INST_GET_A(inst);
        uint32_t b = REDUCT_INST_GET_B(inst);
        uint32_t c = REDUCT_INST_GET_C(inst);
        int32_t sbx = REDUCT_INST_GET_SBX(inst);

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
        case REDUCT_OPCODE_CALL_CONST:
            opName = isConst ? "CALL_K" : "CALL";
            break;
        case REDUCT_OPCODE_RET:
        case REDUCT_OPCODE_RET_CONST:
            opName = isConst ? "RET_K" : "RET";
            break;
        case REDUCT_OPCODE_MOV:
        case REDUCT_OPCODE_MOV_CONST:
            opName = isConst ? "MOV_K" : "MOV";
            break;
        case REDUCT_OPCODE_EQ:
        case REDUCT_OPCODE_EQ_CONST:
            opName = isConst ? "EQ_K" : "EQ";
            break;
        case REDUCT_OPCODE_NEQ:
        case REDUCT_OPCODE_NEQ_CONST:
            opName = isConst ? "NEQ_K" : "NEQ";
            break;
        case REDUCT_OPCODE_LT:
        case REDUCT_OPCODE_LT_CONST:
            opName = isConst ? "LT_K" : "LT";
            break;
        case REDUCT_OPCODE_LE:
        case REDUCT_OPCODE_LE_CONST:
            opName = isConst ? "LE_K" : "LE";
            break;
        case REDUCT_OPCODE_GT:
        case REDUCT_OPCODE_GT_CONST:
            opName = isConst ? "GT_K" : "GT";
            break;
        case REDUCT_OPCODE_GE:
        case REDUCT_OPCODE_GE_CONST:
            opName = isConst ? "GE_K" : "GE";
            break;
        case REDUCT_OPCODE_JEQ:
        case REDUCT_OPCODE_JEQ_CONST:
            opName = isConst ? "JEQ_K" : "JEQ";
            break;
        case REDUCT_OPCODE_JNEQ:
        case REDUCT_OPCODE_JNEQ_CONST:
            opName = isConst ? "JNEQ_K" : "JNEQ";
            break;
        case REDUCT_OPCODE_JLT:
        case REDUCT_OPCODE_JLT_CONST:
            opName = isConst ? "JLT_K" : "JLT";
            break;
        case REDUCT_OPCODE_JLE:
        case REDUCT_OPCODE_JLE_CONST:
            opName = isConst ? "JLE_K" : "JLE";
            break;
        case REDUCT_OPCODE_JGT:
        case REDUCT_OPCODE_JGT_CONST:
            opName = isConst ? "JGT_K" : "JGT";
            break;
        case REDUCT_OPCODE_JGE:
        case REDUCT_OPCODE_JGE_CONST:
            opName = isConst ? "JGE_K" : "JGE";
            break;
        case REDUCT_OPCODE_ADD:
        case REDUCT_OPCODE_ADD_CONST:
            opName = isConst ? "ADD_K" : "ADD";
            break;
        case REDUCT_OPCODE_SUB:
        case REDUCT_OPCODE_SUB_CONST:
            opName = isConst ? "SUB_K" : "SUB";
            break;
        case REDUCT_OPCODE_MUL:
        case REDUCT_OPCODE_MUL_CONST:
            opName = isConst ? "MUL_K" : "MUL";
            break;
        case REDUCT_OPCODE_DIV:
        case REDUCT_OPCODE_DIV_CONST:
            opName = isConst ? "DIV_K" : "DIV";
            break;
        case REDUCT_OPCODE_MOD:
        case REDUCT_OPCODE_MOD_CONST:
            opName = isConst ? "MOD_K" : "MOD";
            break;
        case REDUCT_OPCODE_BAND:
        case REDUCT_OPCODE_BAND_CONST:
            opName = isConst ? "BAND_K" : "BAND";
            break;
        case REDUCT_OPCODE_BOR:
        case REDUCT_OPCODE_BOR_CONST:
            opName = isConst ? "BOR_K" : "BOR";
            break;
        case REDUCT_OPCODE_BXOR:
        case REDUCT_OPCODE_BXOR_CONST:
            opName = isConst ? "BXOR_K" : "BXOR";
            break;
        case REDUCT_OPCODE_BNOT:
        case REDUCT_OPCODE_BNOT_CONST:
            opName = isConst ? "BNOT_K" : "BNOT";
            break;
        case REDUCT_OPCODE_SHL:
        case REDUCT_OPCODE_SHL_CONST:
            opName = isConst ? "SHL_K" : "SHL";
            break;
        case REDUCT_OPCODE_SHR:
        case REDUCT_OPCODE_SHR_CONST:
            opName = isConst ? "SHR_K" : "SHR";
            break;
        case REDUCT_OPCODE_CLOSURE:
            opName = "CLOSURE";
            break;
        case REDUCT_OPCODE_NOP:
            opName = "NOP";
            break;
        case REDUCT_OPCODE_CAPTURE:
        case REDUCT_OPCODE_CAPTURE_CONST:
            opName = isConst ? "CAPTURE_K" : "CAPTURE";
            break;
        case REDUCT_OPCODE_TAILCALL:
        case REDUCT_OPCODE_TAILCALL_CONST:
            opName = isConst ? "TAILCALL_K" : "TAILCALL";
            break;
        case REDUCT_OPCODE_RECUR:
            opName = "RECUR";
            break;
        case REDUCT_OPCODE_TAILRECUR:
            opName = "TAILRECUR";
            break;
        default:
            break;
        }

        fprintf(out, "[%04u] %-12s ", (unsigned int)i, opName);

        switch (op)
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
        case REDUCT_OPCODE_TAILCALL_CONST:
        case REDUCT_OPCODE_CALL:
        case REDUCT_OPCODE_CALL_CONST:
        case REDUCT_OPCODE_CAPTURE:
        case REDUCT_OPCODE_CAPTURE_CONST:
            fprintf(out, "R%-5u %-6u %c%-5u", a, b, isConst ? 'K' : 'R', c);
            break;
        case REDUCT_OPCODE_RET:
        case REDUCT_OPCODE_RET_CONST:
            fprintf(out, "%c%-5u %-6s %-6s", isConst ? 'K' : 'R', c, "", "");
            break;
        case REDUCT_OPCODE_MOV:
        case REDUCT_OPCODE_MOV_CONST:
        case REDUCT_OPCODE_BNOT:
        case REDUCT_OPCODE_BNOT_CONST:
            fprintf(out, "R%-5u %-6s %c%-5u", a, "", isConst ? 'K' : 'R', c);
            break;
        case REDUCT_OPCODE_CLOSURE:
            fprintf(out, "R%-5u %-6s K%-5u", a, "", c);
            break;
        case REDUCT_OPCODE_NOP:
            fprintf(out, "%-6s %-6s %-6s", "", "", "");
            break;
        case REDUCT_OPCODE_JEQ:
        case REDUCT_OPCODE_JEQ_CONST:
        case REDUCT_OPCODE_JNEQ:
        case REDUCT_OPCODE_JNEQ_CONST:
        case REDUCT_OPCODE_JLT:
        case REDUCT_OPCODE_JLT_CONST:
        case REDUCT_OPCODE_JLE:
        case REDUCT_OPCODE_JLE_CONST:
        case REDUCT_OPCODE_JGT:
        case REDUCT_OPCODE_JGT_CONST:
        case REDUCT_OPCODE_JGE:
        case REDUCT_OPCODE_JGE_CONST:
            fprintf(out, "R%-5u %-6s %c%-5u", a, "", isConst ? 'K' : 'R', c);
            break;
        case REDUCT_OPCODE_RECUR:
        case REDUCT_OPCODE_TAILRECUR:
            fprintf(out, "R%-5u %-6u %-6s", a, b, "");
            break;
        default:
            fprintf(out, "R%-5u R%-5u %c%-5u", a, b, isConst ? 'K' : 'R', c);
            break;
        }

        bool hasInlineConst = false;
        if (op == REDUCT_OPCODE_CLOSURE || isConst)
        {
            hasInlineConst = true;
        }

        bool isJump = (op == REDUCT_OPCODE_JMP || op == REDUCT_OPCODE_JMPF || op == REDUCT_OPCODE_JMPT);
        if (isJump)
        {
            int target = (int)i + 1 + sbx;
            fprintf(out, " ; -> [%04u]\n", target);
        }
        else if (hasInlineConst && c < function->constantCount)
        {
            reduct_const_slot_t* slot = &function->constants[c];
            fprintf(out, " ; ");
            if (slot->type == REDUCT_CONST_SLOT_TYPE_HANDLE)
            {
                reduct_handle_t handle = slot->handle;
                if (REDUCT_HANDLE_IS_INT(handle))
                {
                    fprintf(out, "%lld\n", (long long)REDUCT_HANDLE_TO_INT(handle));
                }
                else if (REDUCT_HANDLE_IS_FLOAT(handle))
                {
                    fprintf(out, "%f\n", REDUCT_HANDLE_TO_FLOAT(handle));
                }
                else if (REDUCT_HANDLE_IS_ATOM(handle))
                {
                    reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(handle);
                    if (atom->flags & REDUCT_ATOM_FLAG_QUOTED)
                    {
                        fprintf(out, "\"%.*s\"\n", (int)atom->length, atom->string);
                    }
                    else
                    {
                        fprintf(out, "%.*s\n", (int)atom->length, atom->string);
                    }
                }
                else if (REDUCT_HANDLE_IS_LIST(handle))
                {
                    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
                    fprintf(out, "(list of %u items)\n", (unsigned int)item->length);
                }
                else if (REDUCT_HANDLE_IS_FUNCTION(handle))
                {
                    fprintf(out, "(function %p)\n", (void*)REDUCT_HANDLE_TO_FUNCTION(handle));
                }
                else
                {
                    fprintf(out, "(handle)\n");
                }
            }
            else if (slot->type == REDUCT_CONST_SLOT_TYPE_CAPTURE)
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
            if (slot->type == REDUCT_CONST_SLOT_TYPE_HANDLE)
            {
                reduct_handle_t handle = slot->handle;
                fprintf(out, "[K%03u] ", (unsigned int)i);
                if (REDUCT_HANDLE_IS_INT(handle))
                {
                    fprintf(out, "%lld\n", (long long)REDUCT_HANDLE_TO_INT(handle));
                }
                else if (REDUCT_HANDLE_IS_FLOAT(handle))
                {
                    fprintf(out, "%f\n", REDUCT_HANDLE_TO_FLOAT(handle));
                }
                else if (REDUCT_HANDLE_IS_ATOM(handle))
                {
                    reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(handle);
                    if (atom->flags & REDUCT_ATOM_FLAG_QUOTED)
                    {
                        fprintf(out, "\"%.*s\"\n", (int)atom->length, atom->string);
                    }
                    else
                    {
                        fprintf(out, "%.*s\n", (int)atom->length, atom->string);
                    }
                }
                else if (REDUCT_HANDLE_IS_LIST(handle))
                {
                    reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
                    fprintf(out, "(list of %u items)\n", (unsigned int)item->length);
                }
                else if (REDUCT_HANDLE_IS_FUNCTION(handle))
                {
                    fprintf(out, "(function %p)\n", (void*)REDUCT_HANDLE_TO_FUNCTION(handle));
                }
                else
                {
                    fprintf(out, "(handle)\n");
                }
            }
            else if (slot->type == REDUCT_CONST_SLOT_TYPE_CAPTURE)
            {
                fprintf(out, "[K%03u] (capture %.*s)\n", (unsigned int)i, (int)slot->capture->length,
                    slot->capture->string);
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
        if (slot->type == REDUCT_CONST_SLOT_TYPE_HANDLE)
        {
            reduct_handle_t handle = slot->handle;
            if (REDUCT_HANDLE_IS_FUNCTION(handle))
            {
                reduct_disasm_internal(reduct, REDUCT_HANDLE_TO_FUNCTION(handle), out);
            }
        }
    }
}

REDUCT_API void reduct_disasm(reduct_t* reduct, reduct_handle_t function, FILE* out)
{
    assert(reduct != NULL);
    assert(out != NULL);

    if (!REDUCT_HANDLE_IS_FUNCTION(function))
    {
        return;
    }

    reduct_disasm_internal(reduct, REDUCT_HANDLE_TO_FUNCTION(function), out);
}
