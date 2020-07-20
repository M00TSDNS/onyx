#define BH_DEBUG
#include "onyxsempass.h"
#include "onyxparser.h"

#define CHECK(kind, ...) static b32 check_ ## kind (__VA_ARGS__)

CHECK(block, AstBlock* block);
CHECK(statement_chain, AstNode* start);
CHECK(statement, AstNode* stmt);
CHECK(return, AstReturn* retnode);
CHECK(if, AstIf* ifnode);
CHECK(while, AstWhile* whilenode);
CHECK(for, AstFor* fornode);
CHECK(call, AstCall* call);
CHECK(binaryop, AstBinaryOp* binop);
CHECK(expression, AstTyped* expr);
CHECK(address_of, AstAddressOf* aof);
CHECK(dereference, AstDereference* deref);
CHECK(array_access, AstArrayAccess* expr);
CHECK(global, AstGlobal* global);
CHECK(function, AstFunction* func);
CHECK(overloaded_function, AstOverloadedFunction* func);

static inline void fill_in_type(AstTyped* node) {
    if (node->type == NULL)
        node->type = type_build_from_ast(semstate.allocator, node->type_node);
}

CHECK(return, AstReturn* retnode) {
    if (retnode->expr) {
        if (check_expression(retnode->expr)) return 1;

        if (!types_are_compatible(retnode->expr->type, semstate.expected_return_type)) {
            onyx_message_add(Msg_Type_Function_Return_Mismatch,
                    retnode->expr->token->pos,
                    type_get_name(retnode->expr->type),
                    type_get_name(semstate.expected_return_type));
            return 1;
        }
    } else {
        if (semstate.expected_return_type->Basic.size > 0) {
            onyx_message_add(Msg_Type_Literal,
                    retnode->token->pos,
                    "returning from non-void function without value");
            return 1;
        }
    }

    return 0;
}

CHECK(if, AstIf* ifnode) {
    if (check_expression(ifnode->cond)) return 1;

    if (!type_is_bool(ifnode->cond->type)) {
        onyx_message_add(Msg_Type_Literal,
                ifnode->cond->token->pos,
                "expected boolean type for condition");
        return 1;
    }

    if (ifnode->true_stmt)  if (check_statement(ifnode->true_stmt))  return 1;
    if (ifnode->false_stmt) if (check_statement(ifnode->false_stmt)) return 1;

    return 0;
}

CHECK(while, AstWhile* whilenode) {
    if (check_expression(whilenode->cond)) return 1;

    if (!type_is_bool(whilenode->cond->type)) {
        onyx_message_add(Msg_Type_Literal,
                whilenode->cond->token->pos,
                "expected boolean type for condition");
        return 1;
    }

    return check_statement(whilenode->stmt);
}

CHECK(for, AstFor* fornode) {
    if (check_expression(fornode->start)) return 1;
    if (check_expression(fornode->end)) return 1;
    if (fornode->step)
        if (check_expression(fornode->step)) return 1;

    // HACK
    if (!types_are_compatible(fornode->start->type, &basic_types[Basic_Kind_I32])) {
        onyx_message_add(Msg_Type_Literal,
                fornode->start->token->pos,
                "expected expression of type i32 for start");
        return 1;
    }

    if (!types_are_compatible(fornode->end->type, &basic_types[Basic_Kind_I32])) {
        onyx_message_add(Msg_Type_Literal,
                fornode->end->token->pos,
                "expected expression of type i32 for end");
        return 1;
    }

    if (fornode->step)
        if (!types_are_compatible(fornode->step->type, &basic_types[Basic_Kind_I32])) {
            onyx_message_add(Msg_Type_Literal,
                    fornode->start->token->pos,
                    "expected expression of type i32 for step");
            return 1;
        }


    if (check_statement(fornode->stmt)) return 1;

    return 0;
}

static AstTyped* match_overloaded_function(AstCall* call, AstOverloadedFunction* ofunc) {
    bh_arr_each(AstTyped *, node, ofunc->overloads) {
        AstFunction* overload = (AstFunction *) *node;

        fill_in_type((AstTyped *) overload);

        TypeFunction* ol_type = &overload->type->Function;

        if (ol_type->param_count != call->arg_count) continue;

        AstArgument* arg = call->arguments;
        Type** param_type = ol_type->params;
        while (arg != NULL) {
            fill_in_type((AstTyped *) arg);

            if (!types_are_compatible(*param_type, arg->type)) goto no_match;

            param_type++;
            arg = (AstArgument *) arg->next;
        }

        return (AstTyped *) overload;

no_match:
        continue;
    }

    onyx_message_add(Msg_Type_Literal,
            call->token->pos,
            "unable to match overloaded function");

    return NULL;
}

CHECK(call, AstCall* call) {
    AstFunction* callee = (AstFunction *) call->callee;

    if (callee->kind == Ast_Kind_Symbol) {
        onyx_message_add(Msg_Type_Unresolved_Symbol,
                callee->token->pos,
                callee->token->text, callee->token->length);
        return 1;
    }

    // NOTE: Check arguments
    AstArgument* actual_param = call->arguments;
    while (actual_param != NULL) {
        if (check_expression((AstTyped *) actual_param)) return 1;
        actual_param = (AstArgument *) actual_param->next;
    }

    if (callee->kind == Ast_Kind_Overloaded_Function) {
        call->callee = (AstNode *) match_overloaded_function(call, (AstOverloadedFunction *) callee);
        callee = (AstFunction *) call->callee;

        if (callee == NULL) return 1;
    }

    // NOTE: Build callee's type
    fill_in_type((AstTyped *) callee);

    if (callee->type->kind != Type_Kind_Function) {
        onyx_message_add(Msg_Type_Call_Non_Function,
                call->token->pos,
                callee->token->text, callee->token->length);
        return 1;
    }

    // NOTE: If we calling an intrinsic function, translate the
    // call into an intrinsic call node.
    if (callee->flags & Ast_Flag_Intrinsic) {
        call->kind = Ast_Kind_Intrinsic_Call;
        call->callee = NULL;

        token_toggle_end(callee->intrinsic_name);

        char* intr_name = callee->intrinsic_name->text;
        OnyxIntrinsic intrinsic = ONYX_INTRINSIC_UNDEFINED;

        if (!strcmp("memory_size", intr_name))       intrinsic = ONYX_INTRINSIC_MEMORY_SIZE;
        else if (!strcmp("memory_grow", intr_name))  intrinsic = ONYX_INTRINSIC_MEMORY_GROW;

        else if (!strcmp("clz_i32", intr_name))      intrinsic = ONYX_INTRINSIC_I32_CLZ;
        else if (!strcmp("ctz_i32", intr_name))      intrinsic = ONYX_INTRINSIC_I32_CTZ;
        else if (!strcmp("popcnt_i32", intr_name))   intrinsic = ONYX_INTRINSIC_I32_POPCNT;
        else if (!strcmp("and_i32", intr_name))      intrinsic = ONYX_INTRINSIC_I32_AND;
        else if (!strcmp("or_i32", intr_name))       intrinsic = ONYX_INTRINSIC_I32_OR;
        else if (!strcmp("xor_i32", intr_name))      intrinsic = ONYX_INTRINSIC_I32_XOR;
        else if (!strcmp("shl_i32", intr_name))      intrinsic = ONYX_INTRINSIC_I32_SHL;
        else if (!strcmp("slr_i32", intr_name))      intrinsic = ONYX_INTRINSIC_I32_SLR;
        else if (!strcmp("sar_i32", intr_name))      intrinsic = ONYX_INTRINSIC_I32_SAR;
        else if (!strcmp("rotl_i32", intr_name))     intrinsic = ONYX_INTRINSIC_I32_ROTL;
        else if (!strcmp("rotr_i32", intr_name))     intrinsic = ONYX_INTRINSIC_I32_ROTR;

        else if (!strcmp("clz_i64", intr_name))      intrinsic = ONYX_INTRINSIC_I64_CLZ;
        else if (!strcmp("ctz_i64", intr_name))      intrinsic = ONYX_INTRINSIC_I64_CTZ;
        else if (!strcmp("popcnt_i64", intr_name))   intrinsic = ONYX_INTRINSIC_I64_POPCNT;
        else if (!strcmp("and_i64", intr_name))      intrinsic = ONYX_INTRINSIC_I64_AND;
        else if (!strcmp("or_i64", intr_name))       intrinsic = ONYX_INTRINSIC_I64_OR;
        else if (!strcmp("xor_i64", intr_name))      intrinsic = ONYX_INTRINSIC_I64_XOR;
        else if (!strcmp("shl_i64", intr_name))      intrinsic = ONYX_INTRINSIC_I64_SHL;
        else if (!strcmp("slr_i64", intr_name))      intrinsic = ONYX_INTRINSIC_I64_SLR;
        else if (!strcmp("sar_i64", intr_name))      intrinsic = ONYX_INTRINSIC_I64_SAR;
        else if (!strcmp("rotl_i64", intr_name))     intrinsic = ONYX_INTRINSIC_I64_ROTL;
        else if (!strcmp("rotr_i64", intr_name))     intrinsic = ONYX_INTRINSIC_I64_ROTR;

        else if (!strcmp("abs_f32", intr_name))      intrinsic = ONYX_INTRINSIC_F32_ABS;
        else if (!strcmp("ceil_f32", intr_name))     intrinsic = ONYX_INTRINSIC_F32_CEIL;
        else if (!strcmp("floor_f32", intr_name))    intrinsic = ONYX_INTRINSIC_F32_FLOOR;
        else if (!strcmp("trunc_f32", intr_name))    intrinsic = ONYX_INTRINSIC_F32_TRUNC;
        else if (!strcmp("nearest_f32", intr_name))  intrinsic = ONYX_INTRINSIC_F32_NEAREST;
        else if (!strcmp("sqrt_f32", intr_name))     intrinsic = ONYX_INTRINSIC_F32_SQRT;
        else if (!strcmp("min_f32", intr_name))      intrinsic = ONYX_INTRINSIC_F32_MIN;
        else if (!strcmp("max_f32", intr_name))      intrinsic = ONYX_INTRINSIC_F32_MAX;
        else if (!strcmp("copysign_f32", intr_name)) intrinsic = ONYX_INTRINSIC_F32_COPYSIGN;

        else if (!strcmp("abs_f64", intr_name))      intrinsic = ONYX_INTRINSIC_F64_ABS;
        else if (!strcmp("ceil_f64", intr_name))     intrinsic = ONYX_INTRINSIC_F64_CEIL;
        else if (!strcmp("floor_f64", intr_name))    intrinsic = ONYX_INTRINSIC_F64_FLOOR;
        else if (!strcmp("trunc_f64", intr_name))    intrinsic = ONYX_INTRINSIC_F64_TRUNC;
        else if (!strcmp("nearest_f64", intr_name))  intrinsic = ONYX_INTRINSIC_F64_NEAREST;
        else if (!strcmp("sqrt_f64", intr_name))     intrinsic = ONYX_INTRINSIC_F64_SQRT;
        else if (!strcmp("min_f64", intr_name))      intrinsic = ONYX_INTRINSIC_F64_MIN;
        else if (!strcmp("max_f64", intr_name))      intrinsic = ONYX_INTRINSIC_F64_MAX;
        else if (!strcmp("copysign_f64", intr_name)) intrinsic = ONYX_INTRINSIC_F64_COPYSIGN;

        ((AstIntrinsicCall *)call)->intrinsic = intrinsic;

        token_toggle_end(callee->intrinsic_name);
    }

    call->type = callee->type->Function.return_type;

    AstLocal* formal_param = callee->params;
    actual_param = call->arguments;

    i32 arg_pos = 0;
    while (formal_param != NULL && actual_param != NULL) {
        fill_in_type((AstTyped *) formal_param);

        if (!types_are_compatible(formal_param->type, actual_param->type)) {
            onyx_message_add(Msg_Type_Function_Param_Mismatch,
                    actual_param->token->pos,
                    callee->token->text, callee->token->length,
                    type_get_name(formal_param->type),
                    arg_pos,
                    type_get_name(actual_param->type));
            return 1;
        }

        arg_pos++;
        formal_param = (AstLocal *) formal_param->next;
        actual_param = (AstArgument *) actual_param->next;
    }

    if (formal_param != NULL && actual_param == NULL) {
        onyx_message_add(Msg_Type_Literal,
                call->token->pos,
                "too few arguments to function call");
        return 1;
    }

    if (formal_param == NULL && actual_param != NULL) {
        onyx_message_add(Msg_Type_Literal,
                call->token->pos,
                "too many arguments to function call");
        return 1;
    }

    return 0;
}

CHECK(binaryop, AstBinaryOp* binop) {
    if (check_expression(binop->left)) return 1;
    if (check_expression(binop->right)) return 1;

    if (binop_is_assignment(binop)) {
        if (!is_lval((AstNode *) binop->left)) {
            onyx_message_add(Msg_Type_Not_Lval,
                    binop->left->token->pos,
                    binop->left->token->text, binop->left->token->length);
            return 1;
        }

        if ((binop->left->flags & Ast_Flag_Const) != 0 && binop->left->type != NULL) {
            onyx_message_add(Msg_Type_Assign_Const,
                    binop->token->pos,
                    binop->left->token->text, binop->left->token->length);
            return 1;
        }

        if (binop->operation == Binary_Op_Assign) {
            // NOTE: Raw assignment
            if (binop->left->type == NULL) {
                binop->left->type = binop->right->type;
            }

        } else {
            // NOTE: +=, -=, ...

            AstBinaryOp* binop_node = onyx_ast_node_new(
                    semstate.node_allocator,
                    sizeof(AstBinaryOp),
                    Ast_Kind_Binary_Op);

            binop_node->token = binop->token;
            binop_node->left  = binop->left;
            binop_node->right = binop->right;
            binop_node->type  = binop->right->type;

            if      (binop->operation == Binary_Op_Assign_Add)      binop_node->operation = Binary_Op_Add;
            else if (binop->operation == Binary_Op_Assign_Minus)    binop_node->operation = Binary_Op_Minus;
            else if (binop->operation == Binary_Op_Assign_Multiply) binop_node->operation = Binary_Op_Multiply;
            else if (binop->operation == Binary_Op_Assign_Divide)   binop_node->operation = Binary_Op_Divide;
            else if (binop->operation == Binary_Op_Assign_Modulus)  binop_node->operation = Binary_Op_Modulus;

            binop->right = (AstTyped *) binop_node;
            binop->operation = Binary_Op_Assign;
        }

    } else {
        if (type_is_pointer(binop->left->type)
                || type_is_pointer(binop->right->type)) {
            onyx_message_add(Msg_Type_Literal,
                    binop->token->pos,
                    "binary operations are not supported for pointers (yet).");
            return 1;
        }
    }

    if (binop->left->type == NULL) {
        onyx_message_add(Msg_Type_Unresolved_Type,
                binop->token->pos,
                binop->left->token->text, binop->left->token->length);
        return 1;
    }

    if (binop->right->type == NULL) {
        onyx_message_add(Msg_Type_Unresolved_Type,
                binop->token->pos,
                binop->right->token->text, binop->right->token->length);
        return 1;
    }


    if (!types_are_compatible(binop->left->type, binop->right->type)) {
        onyx_message_add(Msg_Type_Binop_Mismatch,
                binop->token->pos,
                type_get_name(binop->left->type),
                type_get_name(binop->right->type));
        return 1;
    }

    if (binop->operation >= Binary_Op_Equal
            && binop->operation <= Binary_Op_Greater_Equal) {
        binop->type = &basic_types[Basic_Kind_Bool];
    } else {
        binop->type = binop->left->type;
    }

    return 0;
}

CHECK(address_of, AstAddressOf* aof) {
    if (check_expression(aof->expr)) return 1;

    if (aof->expr->kind != Ast_Kind_Array_Access
            && aof->expr->kind != Ast_Kind_Dereference) {
        onyx_message_add(Msg_Type_Literal,
                aof->token->pos,
                "cannot take the address of this");
        return 1;
    }

    aof->type = type_make_pointer(semstate.allocator, aof->expr->type);

    return 0;
}

CHECK(dereference, AstDereference* deref) {
    if (check_expression(deref->expr)) return 1;

    if (!type_is_pointer(deref->expr->type)) {
        onyx_message_add(Msg_Type_Literal,
                deref->token->pos,
                "cannot dereference non-pointer");
        return 1;
    }

    if (deref->expr->type == basic_type_rawptr.type) {
        onyx_message_add(Msg_Type_Literal,
                deref->token->pos,
                "cannot dereference rawptr");
        return 1;
    }

    deref->type = deref->expr->type->Pointer.elem;

    return 0;
}

CHECK(array_access, AstArrayAccess* aa) {
    if (check_expression(aa->addr)) return 1;
    if (check_expression(aa->expr)) return 1;

    if (!type_is_pointer(aa->addr->type)) {
        onyx_message_add(Msg_Type_Literal,
                aa->token->pos,
                "expected pointer type for left of array access");
        return 1;
    }

    if (aa->expr->type->kind != Type_Kind_Basic
            || (aa->expr->type->Basic.flags & Basic_Flag_Integer) == 0) {
        onyx_message_add(Msg_Type_Literal,
                aa->token->pos,
                "expected integer type for index");
        return 1;
    }

    aa->type = aa->addr->type->Pointer.elem;
    aa->elem_size = aa->type->Basic.size;

    return 0;
}

CHECK(expression, AstTyped* expr) {
    if (expr->kind > Ast_Kind_Type_Start && expr->kind < Ast_Kind_Type_End) {
        onyx_message_add(Msg_Type_Literal,
                (OnyxFilePos) { 0 },
                "type used as part of an expression");
        return 1;
    }

    fill_in_type(expr);

    i32 retval = 0;
    switch (expr->kind) {
        case Ast_Kind_Binary_Op: retval = check_binaryop((AstBinaryOp *) expr); break;

        case Ast_Kind_Unary_Op:
            retval = check_expression(((AstUnaryOp *) expr)->expr);

            if (((AstUnaryOp *) expr)->operation != Unary_Op_Cast) {
                expr->type = ((AstUnaryOp *) expr)->expr->type;
            }
            break;

        case Ast_Kind_Call:  retval = check_call((AstCall *) expr); break;
        case Ast_Kind_Block: retval = check_block((AstBlock *) expr); break;

        case Ast_Kind_Symbol:
            onyx_message_add(Msg_Type_Unresolved_Symbol,
                    expr->token->pos,
                    expr->token->text, expr->token->length);
            retval = 1;
            break;

        case Ast_Kind_Param:
            if (expr->type == NULL) {
                onyx_message_add(Msg_Type_Literal,
                        expr->token->pos,
                        "local variable with unknown type");
                retval = 1;
            }
            break;

        case Ast_Kind_Local: break;

        case Ast_Kind_Address_Of:   retval = check_address_of((AstAddressOf *) expr); break;
        case Ast_Kind_Dereference:  retval = check_dereference((AstDereference *) expr); break;
        case Ast_Kind_Array_Access: retval = check_array_access((AstArrayAccess *) expr); break;

        case Ast_Kind_Global:
            if (expr->type == NULL) {
                onyx_message_add(Msg_Type_Literal,
                        expr->token->pos,
                        "global with unknown type");
                retval = 1;
            }
            break;

        case Ast_Kind_Argument:
            retval = check_expression(((AstArgument *) expr)->value);
            expr->type = ((AstArgument *) expr)->value->type;
            break;

        case Ast_Kind_NumLit:
            // NOTE: Literal types should have been decided
            // in the parser (for now).
            assert(expr->type != NULL);
            break;

        case Ast_Kind_StrLit: break;
        case Ast_Kind_Function: break;
        case Ast_Kind_Overloaded_Function: break;

        default:
            retval = 1;
            DEBUG_HERE;
            break;
    }

    return retval;
}

CHECK(global, AstGlobal* global) {
    fill_in_type((AstTyped *) global);

    if (global->type == NULL) {
        onyx_message_add(Msg_Type_Unresolved_Type,
                global->token->pos,
                global->exported_name->text,
                global->exported_name->length);

        return 1;
    }

    return 0;
}

CHECK(statement, AstNode* stmt) {
    switch (stmt->kind) {
        case Ast_Kind_Return:     return check_return((AstReturn *) stmt);
        case Ast_Kind_If:         return check_if((AstIf *) stmt);
        case Ast_Kind_While:      return check_while((AstWhile *) stmt);
        case Ast_Kind_For:        return check_for((AstFor *) stmt);
        case Ast_Kind_Call:       return check_call((AstCall *) stmt);
        case Ast_Kind_Block:      return check_block((AstBlock *) stmt);

        case Ast_Kind_Break:      return 0;
        case Ast_Kind_Continue:   return 0;

        default:
            stmt->flags |= Ast_Flag_Expr_Ignored;
            return check_expression((AstTyped *) stmt);
    }
}

CHECK(statement_chain, AstNode* start) {
    while (start) {
        if (check_statement(start)) return 1;
        start = start->next;
    }

    return 0;
}

CHECK(block, AstBlock* block) {
    if (check_statement_chain(block->body)) return 1;

    bh_table_each_start(AstTyped *, block->scope->symbols);
        if (value->type == NULL) {
            onyx_message_add(Msg_Type_Unresolved_Type,
                    value->token->pos,
                    value->token->text, value->token->length);
            return 1;
        }
    bh_table_each_end;

    return 0;
}

CHECK(function, AstFunction* func) {
    for (AstLocal *param = func->params; param != NULL; param = (AstLocal *) param->next) {
        fill_in_type((AstTyped *) param);

        if (param->type == NULL) {
            onyx_message_add(Msg_Type_Literal,
                    param->token->pos,
                    "function parameter types must be known");
            return 1;
        }

        if (param->type->Basic.size == 0) {
            onyx_message_add(Msg_Type_Literal,
                    param->token->pos,
                    "function parameters must have non-void types");
            return 1;
        }
    }

    fill_in_type((AstTyped *) func);

    if ((func->flags & Ast_Flag_Exported) != 0) {
        if ((func->flags & Ast_Flag_Foreign) != 0) {
            onyx_message_add(Msg_Type_Literal,
                    func->token->pos,
                    "exporting a foreign function");
            return 1;
        }

        if ((func->flags & Ast_Flag_Intrinsic) != 0) {
            onyx_message_add(Msg_Type_Literal,
                    func->token->pos,
                    "exporting a intrinsic function");
            return 1;
        }

        if ((func->flags & Ast_Flag_Inline) != 0) {
            onyx_message_add(Msg_Type_Literal,
                    func->token->pos,
                    "exporting a inlined function");
            return 1;
        }

        if (func->exported_name == NULL) {
            onyx_message_add(Msg_Type_Literal,
                    func->token->pos,
                    "exporting function without a name");
            return 1;
        }
    }

    semstate.expected_return_type = func->type->Function.return_type;
    if (func->body) {
        return check_block(func->body);
    }

    return 0;
}

CHECK(overloaded_function, AstOverloadedFunction* func) {
    bh_arr_each(AstTyped *, node, func->overloads) {
        if ((*node)->kind == Ast_Kind_Overloaded_Function) {
            onyx_message_add(Msg_Type_Literal,
                    (*node)->token->pos,
                    "overload option can not be another overloaded function (yet)");

            return 1;
        }

        if ((*node)->kind != Ast_Kind_Function) {
            onyx_message_add(Msg_Type_Literal,
                    (*node)->token->pos,
                    "overload option not function");

            return 1;
        }
    }

    return 0;
}

CHECK(node, AstNode* node) {
    switch (node->kind) {
        case Ast_Kind_Function:             return check_function((AstFunction *) node);
        case Ast_Kind_Overloaded_Function:  return check_overloaded_function((AstOverloadedFunction *) node);
        case Ast_Kind_Block:                return check_block((AstBlock *) node);
        case Ast_Kind_Return:               return check_return((AstReturn *) node);
        case Ast_Kind_If:                   return check_if((AstIf *) node);
        case Ast_Kind_While:                return check_while((AstWhile *) node);
        case Ast_Kind_Call:                 return check_call((AstCall *) node);
        case Ast_Kind_Binary_Op:            return check_binaryop((AstBinaryOp *) node);
        default:                            return check_expression((AstTyped *) node);
    }
}

void onyx_type_check(ProgramInfo* program) {
    bh_arr_each(Entity, entity, program->entities) {
        switch (entity->type) {
            case Entity_Type_Function:
                if (entity->function->flags & Ast_Kind_Foreign) program->foreign_func_count++;

                if (check_function(entity->function)) return;
                break;

            case Entity_Type_Overloaded_Function:
                if (check_overloaded_function(entity->overloaded_function)) return;
                break;

            case Entity_Type_Global:
                if (entity->global->flags & Ast_Kind_Foreign) program->foreign_global_count++;

                if (check_global(entity->global)) return;
                break;

            case Entity_Type_Expression:
                if (check_expression(entity->expr)) return;
                break;

            case Entity_Type_String_Literal: break;

            default: DEBUG_HERE; break;
        }
    }
}