#include <reduct/atom.h>
#include <reduct/dump.h>
#include <reduct/handle.h>
#include <reduct/inst.h>
#include <reduct/item.h>
#include <reduct/opcode.h>
#include <reduct/optimize.h>

static const char* reduct_dump_opcode_name(reduct_opcode_t op)
{
    bool isConst = (op & REDUCT_OPCODE_MODE_CONST) != 0;
    switch (op & ~REDUCT_OPCODE_MODE_CONST)
    {
    case REDUCT_OPCODE_LIST:
        return "LIST";
    case REDUCT_OPCODE_JMP:
        return "JMP";
    case REDUCT_OPCODE_JMPF:
        return "JMPF";
    case REDUCT_OPCODE_JMPT:
        return "JMPT";
    case REDUCT_OPCODE_CALL:
        return isConst ? "CALL_K" : "CALL";
    case REDUCT_OPCODE_RET:
        return isConst ? "RET_K" : "RET";
    case REDUCT_OPCODE_MOV:
        return isConst ? "MOV_K" : "MOV";
    case REDUCT_OPCODE_EQ:
        return isConst ? "EQ_K" : "EQ";
    case REDUCT_OPCODE_NEQ:
        return isConst ? "NEQ_K" : "NEQ";
    case REDUCT_OPCODE_LT:
        return isConst ? "LT_K" : "LT";
    case REDUCT_OPCODE_LE:
        return isConst ? "LE_K" : "LE";
    case REDUCT_OPCODE_GT:
        return isConst ? "GT_K" : "GT";
    case REDUCT_OPCODE_GE:
        return isConst ? "GE_K" : "GE";
    case REDUCT_OPCODE_JEQ:
        return isConst ? "JEQ_K" : "JEQ";
    case REDUCT_OPCODE_JNEQ:
        return isConst ? "JNEQ_K" : "JNEQ";
    case REDUCT_OPCODE_JLT:
        return isConst ? "JLT_K" : "JLT";
    case REDUCT_OPCODE_JLE:
        return isConst ? "JLE_K" : "JLE";
    case REDUCT_OPCODE_JGT:
        return isConst ? "JGT_K" : "JGT";
    case REDUCT_OPCODE_JGE:
        return isConst ? "JGE_K" : "JGE";
    case REDUCT_OPCODE_ADD:
        return isConst ? "ADD_K" : "ADD";
    case REDUCT_OPCODE_SUB:
        return isConst ? "SUB_K" : "SUB";
    case REDUCT_OPCODE_MUL:
        return isConst ? "MUL_K" : "MUL";
    case REDUCT_OPCODE_DIV:
        return isConst ? "DIV_K" : "DIV";
    case REDUCT_OPCODE_MOD:
        return isConst ? "MOD_K" : "MOD";
    case REDUCT_OPCODE_BAND:
        return isConst ? "BAND_K" : "BAND";
    case REDUCT_OPCODE_BOR:
        return isConst ? "BOR_K" : "BOR";
    case REDUCT_OPCODE_BXOR:
        return isConst ? "BXOR_K" : "BXOR";
    case REDUCT_OPCODE_BNOT:
        return isConst ? "BNOT_K" : "BNOT";
    case REDUCT_OPCODE_SHL:
        return isConst ? "SHL_K" : "SHL";
    case REDUCT_OPCODE_SHR:
        return isConst ? "SHR_K" : "SHR";
    case REDUCT_OPCODE_CLOSURE:
        return "CLOSURE";
    case REDUCT_OPCODE_NOP:
        return "NOP";
    case REDUCT_OPCODE_CAPTURE:
        return isConst ? "CAPTURE_K" : "CAPTURE";
    case REDUCT_OPCODE_TAILCALL:
        return isConst ? "TAILCALL_K" : "TAILCALL";
    case REDUCT_OPCODE_RECUR:
        return "RECUR";
    case REDUCT_OPCODE_TAILRECUR:
        return "TAILRECUR";
    default:
        return "UNKNOWN";
    }
}

static void reduct_dump_print_handle(reduct_handle_t handle, FILE* out)
{
    if (REDUCT_HANDLE_IS_NUMBER(handle))
    {
        fprintf(out, "%f", REDUCT_HANDLE_TO_NUMBER(handle));
    }
    else if (REDUCT_HANDLE_IS_ATOM(handle))
    {
        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(handle);
        if (atom->flags & REDUCT_ATOM_FLAG_QUOTED)
        {
            fprintf(out, "\"%.*s\"", (int)atom->length, atom->string);
        }
        else
        {
            fprintf(out, "%.*s", (int)atom->length, atom->string);
        }
    }
    else if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
        fprintf(out, "(list of %u items)", (unsigned int)item->length);
    }
    else if (REDUCT_HANDLE_IS_FUNCTION(handle))
    {
        fprintf(out, "(function %p)", (void*)REDUCT_HANDLE_TO_FUNCTION(handle));
    }
    else
    {
        fprintf(out, "(handle)");
    }
}

static void reduct_dump_print_const_slot(reduct_const_slot_t* slot, FILE* out)
{
    if (slot->type == REDUCT_CONST_SLOT_TYPE_STATIC)
    {
        reduct_dump_print_handle(slot->handle, out);
    }
    else if (slot->type == REDUCT_CONST_SLOT_TYPE_CAPTURE)
    {
        fprintf(out, "(capture)");
    }
    else
    {
        fprintf(out, "(none)");
    }
}

static void reduct_dump_internal(reduct_t* reduct, reduct_function_t* function, FILE* out)
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
    fprintf(out, "Register count: %u\n", (unsigned int)function->registerCount);
    fprintf(out, "--------------------------------------------------------------------------------\n");

    for (size_t i = 0; i < function->instCount; ++i)
    {
        reduct_inst_t inst = function->insts[i];
        reduct_opcode_t op = REDUCT_INST_GET_OP(inst);
        bool isConst = (op & REDUCT_OPCODE_MODE_CONST) != 0;
        uint32_t a = REDUCT_INST_GET_A(inst);
        uint32_t b = REDUCT_INST_GET_B(inst);
        uint32_t c = REDUCT_INST_GET_C(inst);
        int32_t sax = REDUCT_INST_GET_SAX(inst);

        const char* opName = reduct_dump_opcode_name(op);

        fprintf(out, "[%04u] %-12s ", (unsigned int)i, opName);

        switch (op)
        {
        case REDUCT_OPCODE_LIST:
            fprintf(out, "R%-5u %-6u", a, b);
            break;
        case REDUCT_OPCODE_JMP:
            fprintf(out, "%-6d %-6s %-6s", sax, "", "");
            break;
        case REDUCT_OPCODE_JMPF:
        case REDUCT_OPCODE_JMPT:
            fprintf(out, "%-6d %-6s R%-5u", sax, "", c);
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
            fprintf(out, "R%-5u %-6s %c%-5u", b, "", isConst ? 'K' : 'R', c);
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
            int target = (int)i + 1 + sax;
            fprintf(out, " ; -> [%04u]\n", target);
        }
        else if (hasInlineConst && c < function->constantCount)
        {
            fprintf(out, " ; ");
            reduct_dump_print_const_slot(&function->constants[c], out);
            fprintf(out, "\n");
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
            fprintf(out, "[K%03u] ", (unsigned int)i);
            reduct_dump_print_const_slot(&function->constants[i], out);
            fprintf(out, "\n");
        }
    }

    fprintf(out, "================================================================================\n");

    for (uint16_t i = 0; i < function->constantCount; ++i)
    {
        reduct_const_slot_t* slot = &function->constants[i];
        if (slot->type == REDUCT_CONST_SLOT_TYPE_STATIC)
        {
            reduct_handle_t handle = slot->handle;
            if (REDUCT_HANDLE_IS_FUNCTION(handle))
            {
                reduct_dump_internal(reduct, REDUCT_HANDLE_TO_FUNCTION(handle), out);
            }
        }
    }
}

REDUCT_API void reduct_dump_function(reduct_t* reduct, reduct_handle_t function, FILE* out)
{
    assert(reduct != NULL);
    assert(out != NULL);

    if (!REDUCT_HANDLE_IS_FUNCTION(function))
    {
        return;
    }

    reduct_dump_internal(reduct, REDUCT_HANDLE_TO_FUNCTION(function), out);
}

static void reduct_dump_escape_gv_record(const char* str, size_t len, FILE* out)
{
    for (size_t i = 0; i < len; i++)
    {
        char c = str[i];
        if (c == '<' || c == '>' || c == '|' || c == '{' || c == '}' || c == '"' || c == '\\')
        {
            fprintf(out, "\\%c", c);
        }
        else
        {
            fputc(c, out);
        }
    }
}

static void reduct_dump_gv_print_handle(reduct_handle_t handle, FILE* out)
{
    if (REDUCT_HANDLE_IS_NUMBER(handle))
    {
        fprintf(out, "%f", REDUCT_HANDLE_TO_NUMBER(handle));
    }
    else if (REDUCT_HANDLE_IS_ATOM(handle))
    {
        reduct_atom_t* atom = REDUCT_HANDLE_TO_ATOM(handle);
        if (atom->flags & REDUCT_ATOM_FLAG_QUOTED)
        {
            fprintf(out, "\\\"");
            reduct_dump_escape_gv_record(atom->string, atom->length, out);
            fprintf(out, "\\\"");
        }
        else
        {
            reduct_dump_escape_gv_record(atom->string, atom->length, out);
        }
    }
    else if (REDUCT_HANDLE_IS_LIST(handle))
    {
        reduct_item_t* item = REDUCT_HANDLE_TO_ITEM(handle);
        fprintf(out, "(list of %u items)", (unsigned int)item->length);
    }
    else if (REDUCT_HANDLE_IS_FUNCTION(handle))
    {
        fprintf(out, "(function %p)", (void*)REDUCT_HANDLE_TO_FUNCTION(handle));
    }
    else
    {
        fprintf(out, "(handle)");
    }
}

static void reduct_dump_gv_node(reduct_t* reduct, reduct_rvsdg_node_t* node, FILE* out)
{
    bool isStructural = node->regionCount > 0;

    const reduct_rvsdg_node_info_t* info = reduct_rvsdg_node_get_info(node->type);
    const char* fillcolor = info->color;

    if (isStructural)
    {
        const char* label = info->name;

        fprintf(out, "  subgraph cluster_node_%p {\n", (void*)node);
        fprintf(out, "    label=\"%s\";\n", label);
        fprintf(out, "    style=filled; fillcolor=\"%s\"; color=black;\n", fillcolor);

        if (node->inputCount > 0)
        {
            fprintf(out, "    node_%p_in [shape=record, style=filled, fillcolor=\"#ffffff\", label=\"", (void*)node);
            for (uint16_t i = 0; i < node->inputCount; i++)
            {
                if (i > 0)
                    fprintf(out, "|");
                fprintf(out, "<in_%u> in %u", i, i);
            }
            fprintf(out, "\"];\n");
        }

        reduct_rvsdg_region_t* region = node->firstRegion;
        for (uint16_t i = 0; i < node->regionCount && region != NULL; i++, region = region->next)
        {
            fprintf(out, "    subgraph cluster_region_%p {\n", (void*)region);
            fprintf(out, "      label=\"Region %u\";\n      style=filled; fillcolor=\"#fafafa\"; color=gray;\n", i);

            if (region->argumentCount > 0)
            {
                fprintf(out, "      region_%p_args [shape=record, style=filled, fillcolor=\"#eeeeee\", label=\"",
                    (void*)region);
                for (uint16_t j = 0; j < region->argumentCount; j++)
                {
                    if (j > 0)
                        fprintf(out, "|");
                    fprintf(out, "<arg_%u> arg %u", j, j);
                }
                fprintf(out, "\"];\n");
            }

            for (reduct_rvsdg_node_t* child = region->firstNode; child != NULL; child = child->next)
            {
                reduct_dump_gv_node(reduct, child, out);
            }

            fprintf(out,
                "      region_%p_res [shape=record, style=filled, fillcolor=\"#eeeeee\", label=\"<res> res\"];\n",
                (void*)region);
            fprintf(out, "    }\n");
        }

        fprintf(out, "    node_%p_out [shape=record, style=filled, fillcolor=\"#ffffff\", label=\"<out> out\"];\n",
            (void*)node);

        if (node->inputCount > 0 && node->firstRegion != NULL && node->firstRegion->argumentCount > 0)
        {
            fprintf(out, "    node_%p_in -> region_%p_args [style=invis];\n", (void*)node, (void*)node->firstRegion);
        }

        region = node->firstRegion;
        while (region != NULL && region->next != NULL)
        {
            if (region->next->argumentCount > 0)
            {
                fprintf(out, "    region_%p_res -> region_%p_args [style=invis];\n", (void*)region,
                    (void*)region->next);
            }
            region = region->next;
        }

        reduct_rvsdg_region_t* last = node->firstRegion;
        if (last != NULL)
        {
            while (last->next != NULL)
                last = last->next;
            fprintf(out, "    region_%p_res -> node_%p_out [style=invis];\n", (void*)last, (void*)node);
        }

        fprintf(out, "  }\n");
    }
    else
    {
        fprintf(out, "  node_%p [style=filled, fillcolor=\"%s\", label=\"", (void*)node, fillcolor);

        if (node->inputCount > 0 || node->output != NULL)
        {
            fprintf(out, "{");
        }

        if (node->inputCount > 0)
        {
            fprintf(out, "{");
            for (uint16_t i = 0; i < node->inputCount; i++)
            {
                if (i > 0)
                    fprintf(out, "|");
                fprintf(out, "<in_%u> in %u", i, i);
            }
            fprintf(out, "} | ");
        }

        if (node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_OPCODE)
        {
            fprintf(out, "OP: %s", reduct_dump_opcode_name(node->opcode & ~REDUCT_OPCODE_MODE_CONST));
        }
        else if (node->type == REDUCT_RVSDG_NODE_TYPE_SIMPLE_CONST)
        {
            fprintf(out, "CONST: ");
            reduct_dump_gv_print_handle(node->constant, out);
        }
        else
        {
            fprintf(out, "INVALID");
        }

        if (node->output != NULL)
        {
            fprintf(out, " | {<out> out}");
        }
        if (node->inputCount > 0 || node->output != NULL)
        {
            fprintf(out, "}");
        }
        fprintf(out, "\"];\n");
    }
}

static void reduct_dump_gv_edges(reduct_rvsdg_node_t** nodes, size_t count, FILE* out)
{
    for (size_t n = 0; n < count; n++)
    {
        reduct_rvsdg_node_t* node = nodes[n];
        if (node->output != NULL)
        {
            for (reduct_rvsdg_edge_t* edge = node->output->uses; edge != NULL; edge = edge->next)
            {
                if (node->regionCount > 0)
                {
                    fprintf(out, "  node_%p_out:out -> ", (void*)node);
                }
                else
                {
                    fprintf(out, "  node_%p:out -> ", (void*)node);
                }

                if (edge->user->ownerKind == REDUCT_RVSDG_OWNER_NODE)
                {
                    if (edge->user->node->regionCount > 0)
                    {
                        fprintf(out, "node_%p_in:in_%u", (void*)edge->user->node, edge->user->index);
                    }
                    else
                    {
                        fprintf(out, "node_%p:in_%u", (void*)edge->user->node, edge->user->index);
                    }
                }
                else
                {
                    fprintf(out, "region_%p_res:res", (void*)edge->user->region);
                }
                fprintf(out, ";\n");
            }
        }

        for (reduct_rvsdg_region_t* region = node->firstRegion; region != NULL; region = region->next)
        {
            for (reduct_rvsdg_origin_t* arg = region->firstArgument; arg != NULL; arg = arg->next)
            {
                for (reduct_rvsdg_edge_t* edge = arg->uses; edge != NULL; edge = edge->next)
                {
                    fprintf(out, "  region_%p_args:arg_%u -> ", (void*)region, arg->index);
                    if (edge->user->ownerKind == REDUCT_RVSDG_OWNER_NODE)
                    {
                        if (edge->user->node->regionCount > 0)
                        {
                            fprintf(out, "node_%p_in:in_%u", (void*)edge->user->node, edge->user->index);
                        }
                        else
                        {
                            fprintf(out, "node_%p:in_%u", (void*)edge->user->node, edge->user->index);
                        }
                    }
                    else
                    {
                        fprintf(out, "region_%p_res:res", (void*)edge->user->region);
                    }
                    fprintf(out, ";\n");
                }
            }
        }
    }
}

REDUCT_API void reduct_dump_rvsdg(reduct_t* reduct, reduct_handle_t graph, FILE* out)
{
    assert(reduct != NULL);
    assert(out != NULL);

    size_t nodeCount = 0;
    reduct_item_block_t* block = reduct->block;
    while (block != NULL)
    {
        for (uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
        {
            if (block->items[i].type == REDUCT_ITEM_TYPE_RVSDG_NODE)
            {
                nodeCount++;
            }
        }
        block = block->next;
    }

    REDUCT_SCRATCH(reduct, nodes, reduct_rvsdg_node_t*, nodeCount);
    size_t idx = 0;
    block = reduct->block;
    while (block != NULL)
    {
        for (uint32_t i = 0; i < REDUCT_ITEM_BLOCK_MAX; i++)
        {
            if (block->items[i].type == REDUCT_ITEM_TYPE_RVSDG_NODE)
            {
                nodes[idx++] = &block->items[i].rvsdgNode;
            }
        }
        block = block->next;
    }

    fprintf(out, "digraph RVSDG {\n");
    fprintf(out, "  node [shape=record, fontname=\"Courier\"];\n");
    fprintf(out, "  edge [fontname=\"Courier\"];\n");
    fprintf(out, "  rankdir=TB;\n");
    fprintf(out, "  compound=true;\n");
    fprintf(out, "  label=\"RVSDG Graph: %p\";\n", (void*)REDUCT_HANDLE_TO_RVSDG_NODE(graph));

    for (size_t n = 0; n < nodeCount; n++)
    {
        if (nodes[n]->parent == NULL)
        {
            reduct_dump_gv_node(reduct, nodes[n], out);
        }
    }

    reduct_dump_gv_edges(nodes, nodeCount, out);

    fprintf(out, "}\n");

    REDUCT_SCRATCH_FREE(reduct, nodes);
}