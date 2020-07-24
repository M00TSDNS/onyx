#define BH_DEBUG
#include "onyxsempass.h"
#include "onyxparser.h"
#include "onyxutils.h"

AstBasicType basic_type_void   = { { Ast_Kind_Basic_Type, 0, NULL, "void"   }, &basic_types[Basic_Kind_Void]  };
AstBasicType basic_type_bool   = { { Ast_Kind_Basic_Type, 0, NULL, "bool"   }, &basic_types[Basic_Kind_Bool]  };
AstBasicType basic_type_i8     = { { Ast_Kind_Basic_Type, 0, NULL, "i8"     }, &basic_types[Basic_Kind_I8]    };
AstBasicType basic_type_u8     = { { Ast_Kind_Basic_Type, 0, NULL, "u8"     }, &basic_types[Basic_Kind_U8]    };
AstBasicType basic_type_i16    = { { Ast_Kind_Basic_Type, 0, NULL, "i16"    }, &basic_types[Basic_Kind_I16]   };
AstBasicType basic_type_u16    = { { Ast_Kind_Basic_Type, 0, NULL, "u16"    }, &basic_types[Basic_Kind_U16]   };
AstBasicType basic_type_i32    = { { Ast_Kind_Basic_Type, 0, NULL, "i32"    }, &basic_types[Basic_Kind_I32]   };
AstBasicType basic_type_u32    = { { Ast_Kind_Basic_Type, 0, NULL, "u32"    }, &basic_types[Basic_Kind_U32]   };
AstBasicType basic_type_i64    = { { Ast_Kind_Basic_Type, 0, NULL, "i64"    }, &basic_types[Basic_Kind_I64]   };
AstBasicType basic_type_u64    = { { Ast_Kind_Basic_Type, 0, NULL, "u64"    }, &basic_types[Basic_Kind_U64]   };
AstBasicType basic_type_f32    = { { Ast_Kind_Basic_Type, 0, NULL, "f32"    }, &basic_types[Basic_Kind_F32]   };
AstBasicType basic_type_f64    = { { Ast_Kind_Basic_Type, 0, NULL, "f64"    }, &basic_types[Basic_Kind_F64]   };
AstBasicType basic_type_rawptr = { { Ast_Kind_Basic_Type, 0, NULL, "rawptr" }, &basic_types[Basic_Kind_Rawptr] };

static OnyxToken builtin_heap_start_token = { Token_Type_Symbol, 12, "__heap_start ", { 0 } };
AstNumLit builtin_heap_start = { Ast_Kind_NumLit, Ast_Flag_Const, &builtin_heap_start_token, NULL, (AstType *) &basic_type_rawptr, NULL, 0 };

const BuiltinSymbol builtin_symbols[] = {
    { "void",       (AstNode *) &basic_type_void },
    { "bool",       (AstNode *) &basic_type_bool },
    { "i8",         (AstNode *) &basic_type_i8 },
    { "u8",         (AstNode *) &basic_type_u8 },
    { "i16",        (AstNode *) &basic_type_i16 },
    { "u16",        (AstNode *) &basic_type_u16 },
    { "i32",        (AstNode *) &basic_type_i32 },
    { "u32",        (AstNode *) &basic_type_u32 },
    { "i64",        (AstNode *) &basic_type_i64 },
    { "u64",        (AstNode *) &basic_type_u64 },
    { "f32",        (AstNode *) &basic_type_f32 },
    { "f64",        (AstNode *) &basic_type_f64 },
    { "rawptr",     (AstNode *) &basic_type_rawptr },

    { "__heap_start", (AstNode *) &builtin_heap_start },

    { NULL, NULL },
};

static void scope_enter(Scope* new_scope);
static void scope_leave();

static AstType* symres_type(AstType* type);
static void symres_local(AstLocal** local);
static void symres_call(AstCall* call);
static void symres_size_of(AstSizeOf* so);
static void symres_field_access(AstFieldAccess** fa);
static void symres_expression(AstTyped** expr);
static void symres_return(AstReturn* ret);
static void symres_if(AstIf* ifnode);
static void symres_while(AstWhile* whilenode);
static void symres_for(AstFor* fornode);
static void symres_statement_chain(AstNode* walker, AstNode** trailer);
static b32  symres_statement(AstNode* stmt);
static void symres_block(AstBlock* block);
static void symres_function(AstFunction* func);
static void symres_global(AstGlobal* global);
static void symres_overloaded_function(AstOverloadedFunction* ofunc);
static void symres_use_package(AstUsePackage* package);

static void scope_enter(Scope* new_scope) {
    if (new_scope->parent == NULL)
        new_scope->parent = semstate.curr_scope;
    semstate.curr_scope = new_scope;
}

static void scope_leave() {
    semstate.curr_scope = semstate.curr_scope->parent;
}

static AstType* symres_type(AstType* type) {
    if (type == NULL) return NULL;

    if (type->kind == Ast_Kind_Symbol) {
        return (AstType *) symbol_resolve(semstate.curr_scope, ((AstNode *) type)->token);
    }

    if (type->kind == Ast_Kind_Field_Access) {
        AstFieldAccess* field = (AstFieldAccess *) type;
        symres_field_access(&field);
        
        if (!node_is_type((AstNode *) field)) {
            onyx_message_add(Msg_Type_Literal,
                    type->token->pos,
                    "field access did not result in a type");
        }

        return (AstType *) field;
    }

    // NOTE: Already resolved
    if (type->kind == Ast_Kind_Basic_Type) return type;

    if (type->kind == Ast_Kind_Pointer_Type) {
        ((AstPointerType *) type)->elem = symres_type(((AstPointerType *) type)->elem);
        return type;
    }

    if (type->kind == Ast_Kind_Function_Type) {
        AstFunctionType* ftype = (AstFunctionType *) type;

        ftype->return_type = symres_type(ftype->return_type);

        if (ftype->param_count > 0)
            fori (i, 0, ftype->param_count - 1) {
                ftype->params[i] = symres_type(ftype->params[i]);
            }

        return type;
    }

    if (type->kind == Ast_Kind_Struct_Type) {
        AstStructType* s_node = (AstStructType *) type;
        if (s_node->flags & Ast_Flag_Type_Is_Resolved) return type;

        s_node->flags |= Ast_Flag_Type_Is_Resolved;

        bh_arr_each(AstStructMember *, member, s_node->members) {
            (*member)->type_node = symres_type((*member)->type_node);
        }

        return type;
    }

    if (type->kind == Ast_Kind_Array_Type) {
        AstArrayType* a_node = (AstArrayType *) type;

        if (a_node->count_expr) symres_expression(&a_node->count_expr);
        a_node->elem = symres_type(a_node->elem);

        return type;
    }

    assert(("Bad type node", 0));
    return NULL;
}

static void symres_local(AstLocal** local) {
    (*local)->type_node = symres_type((*local)->type_node);

    bh_arr_push(semstate.curr_function->locals, *local);

    symbol_introduce(semstate.curr_scope, (*local)->token, (AstNode *) *local);
}

static void symres_call(AstCall* call) {
    symres_expression((AstTyped **) &call->callee);
    if (call->callee == NULL) return;

    if (call->callee->kind == Ast_Kind_Field_Access) {
        AstFieldAccess* fa = (AstFieldAccess *) call->callee;
        if (fa->expr == NULL) return;

        AstArgument* implicit_arg = onyx_ast_node_new(semstate.node_allocator, sizeof(AstArgument), Ast_Kind_Argument);
        implicit_arg->value = fa->expr;
        implicit_arg->token = fa->expr->token;
        implicit_arg->next = (AstNode *) call->arguments;

        call->callee = symbol_resolve(semstate.curr_scope, fa->token);
        call->arguments = implicit_arg;
        call->arg_count++;
    }

    symres_statement_chain((AstNode *) call->arguments, (AstNode **) &call->arguments);
}

static void symres_size_of(AstSizeOf* so) {
    so->type_node = symres_type(so->type_node);
    so->so_type = symres_type(so->so_type);
}

static void symres_field_access(AstFieldAccess** fa) {
    if ((*fa)->expr == NULL) return;
    symres_expression(&(*fa)->expr);
    if ((*fa)->expr == NULL) return;

    if ((*fa)->expr->kind == Ast_Kind_Package) {
        AstPackage* package = (AstPackage *) (*fa)->expr;
        AstNode* n = symbol_resolve(package->package->scope, (*fa)->token);
        if (n) {
            // NOTE: not field access
            *fa = (AstFieldAccess *) n;
        }
    }
}

static void symres_unaryop(AstUnaryOp** unaryop) {
    if ((*unaryop)->operation == Unary_Op_Cast) {
        (*unaryop)->type_node = symres_type((*unaryop)->type_node);
    }

    symres_expression(&(*unaryop)->expr);
}

static void symres_expression(AstTyped** expr) {
    switch ((*expr)->kind) {
        case Ast_Kind_Binary_Op:
            symres_expression(&((AstBinaryOp *)(*expr))->left);
            symres_expression(&((AstBinaryOp *)(*expr))->right);
            break;

        case Ast_Kind_Unary_Op: symres_unaryop((AstUnaryOp **) expr); break;
        case Ast_Kind_Call: symres_call((AstCall *) *expr); break;
        case Ast_Kind_Block: symres_block((AstBlock *) *expr); break;

        case Ast_Kind_Symbol:
            *expr = (AstTyped *) symbol_resolve(semstate.curr_scope, ((AstNode *) *expr)->token);
            break;


        case Ast_Kind_Function:
        case Ast_Kind_NumLit:
        case Ast_Kind_StrLit:
            (*expr)->type_node = symres_type((*expr)->type_node);
            break;

        case Ast_Kind_Address_Of:   symres_expression(&((AstAddressOf *)(*expr))->expr); break;
        case Ast_Kind_Dereference:  symres_expression(&((AstDereference *)(*expr))->expr); break;
        case Ast_Kind_Field_Access: symres_field_access((AstFieldAccess **) expr); break;
        case Ast_Kind_Size_Of:      symres_size_of((AstSizeOf *)*expr); break;

        case Ast_Kind_Array_Access:
            symres_expression(&((AstArrayAccess *)(*expr))->addr);
            symres_expression(&((AstArrayAccess *)(*expr))->expr);
            break;

        // NOTE: This is a good case, since it means the symbol is already resolved
        case Ast_Kind_Local:
        default: break;
    }
}

static void symres_return(AstReturn* ret) {
    if (ret->expr)
        symres_expression(&ret->expr);
}

static void symres_if(AstIf* ifnode) {
    symres_expression(&ifnode->cond);

    // BUG: This will not work for the following case:
    //  if cond foo := 10
    //  else foo := 20
    //
    // The declaration will cause a problem but semantically the above
    // doesn't make sense.
    if (ifnode->true_stmt != NULL)  symres_statement(ifnode->true_stmt);
    if (ifnode->false_stmt != NULL) symres_statement(ifnode->false_stmt);
}

static void symres_while(AstWhile* whilenode) {
    symres_expression(&whilenode->cond);
    symres_statement(whilenode->stmt);
}

static void symres_for(AstFor* fornode) {
    fornode->scope = scope_create(semstate.node_allocator, semstate.curr_scope);
    scope_enter(fornode->scope);

    bh_arr_push(semstate.curr_function->locals, fornode->var);
    symbol_introduce(semstate.curr_scope, fornode->var->token, (AstNode *) fornode->var);

    symres_expression(&fornode->start);
    symres_expression(&fornode->end);
    if (fornode->step) symres_expression(&fornode->step);

    symres_statement(fornode->stmt);

    scope_leave();
}

// NOTE: Returns 1 if the statment should be removed
static b32 symres_statement(AstNode* stmt) {
    switch (stmt->kind) {
        case Ast_Kind_Local:      symres_local((AstLocal **) &stmt);                return 1;
        case Ast_Kind_Return:     symres_return((AstReturn *) stmt);                return 0;
        case Ast_Kind_If:         symres_if((AstIf *) stmt);                        return 0;
        case Ast_Kind_While:      symres_while((AstWhile *) stmt);                  return 0;
        case Ast_Kind_For:        symres_for((AstFor *) stmt);                      return 0;
        case Ast_Kind_Call:       symres_call((AstCall *) stmt);                    return 0;
        case Ast_Kind_Argument:   symres_expression((AstTyped **) &((AstArgument *)stmt)->value); return 0;
        case Ast_Kind_Block:      symres_block((AstBlock *) stmt);                  return 0;

        case Ast_Kind_Break:      return 0;
        case Ast_Kind_Continue:   return 0;

        default:                  symres_expression((AstTyped **) &stmt);           return 0;
    }
}

static void symres_statement_chain(AstNode* walker, AstNode** trailer) {
    while (walker) {
        if (symres_statement(walker)) {
            *trailer = walker->next;

            AstNode* tmp = walker->next;
            walker->next = NULL;
            walker = tmp;
        } else {
            trailer = &walker->next;
            walker = walker->next;
        }
    }
}

static void symres_block(AstBlock* block) {
    if (block->scope == NULL)
        block->scope = scope_create(semstate.node_allocator, semstate.curr_scope);

    scope_enter(block->scope);

    if (block->body)
        symres_statement_chain(block->body, &block->body);

    scope_leave();
}

static void symres_function(AstFunction* func) {
    if (func->scope == NULL)
        func->scope = scope_create(semstate.node_allocator, semstate.curr_scope);

    scope_enter(func->scope);

    for (AstLocal *param = func->params; param != NULL; param = (AstLocal *) param->next) {
        param->type_node = symres_type(param->type_node);

        symbol_introduce(semstate.curr_scope, param->token, (AstNode *) param);
    }

    if (func->type_node != NULL) {
        func->type_node = symres_type(func->type_node);
    }

    semstate.curr_function = func;
    symres_block(func->body);

    scope_leave();
}

static void symres_global(AstGlobal* global) {
    global->type_node = symres_type(global->type_node);
}

static void symres_overloaded_function(AstOverloadedFunction* ofunc) {
    bh_arr_each(AstTyped *, node, ofunc->overloads) {
        if ((*node)->kind == Ast_Kind_Symbol) {
            *node = (AstTyped *) symbol_resolve(semstate.curr_scope, (*node)->token);
        }
    }
}

static void symres_use_package(AstUsePackage* package) {
    token_toggle_end(package->package->token);
    Package* p = program_info_package_lookup(semstate.program, package->package->token->text);
    token_toggle_end(package->package->token);

    if (p == NULL) {
        onyx_message_add(Msg_Type_Literal,
            package->token->pos,
            "package not found in included source files");
        return;
    }

    if (p->scope == semstate.curr_scope) return;

    if (package->alias != NULL) {
        AstPackage *pac_node = onyx_ast_node_new(semstate.node_allocator, sizeof(AstPackage), Ast_Kind_Package);
        pac_node->package = p;
        pac_node->token = package->alias;

        symbol_introduce(semstate.curr_package->include_scope, package->alias, (AstNode *) pac_node);
    }

    if (package->only != NULL) {
        bh_arr_each(OnyxToken *, tkn, package->only) {

            AstNode* thing = symbol_resolve(p->scope, *tkn);
            if (thing == NULL) {
                onyx_message_add(Msg_Type_Literal,
                    (*tkn)->pos,
                    "not found in package");
                return;
            }
            symbol_introduce(semstate.curr_package->include_scope, *tkn, thing);
        }
    }

    if (package->alias == NULL && package->only == NULL)
        scope_include(semstate.curr_package->include_scope, p->scope);
}

void onyx_resolve_symbols() {

    semstate.curr_scope = semstate.program->global_scope;

    // NOTE: Add types to global scope
    BuiltinSymbol* bsym = (BuiltinSymbol *) &builtin_symbols[0];
    while (bsym->sym != NULL) {
        symbol_builtin_introduce(semstate.curr_scope, bsym->sym, bsym->node);
        bsym++;
    }

    bh_arr_each(Entity, entity, semstate.program->entities) {
        scope_enter(entity->package->scope);
        semstate.curr_package = entity->package;

        switch (entity->type) {
            case Entity_Type_Use_Package:         symres_use_package(entity->use_package); break;
            case Entity_Type_Function:            symres_function(entity->function); break;
            case Entity_Type_Overloaded_Function: symres_overloaded_function(entity->overloaded_function); break;
            case Entity_Type_Global:              symres_global(entity->global); break;
            case Entity_Type_Expression:          symres_expression(&entity->expr); break;
            case Entity_Type_Struct:              symres_type((AstType *) entity->struct_type); break;

            default: break;
        }

        scope_leave();
    }
}
