// This file is directly included in src/onxywasm.c
// It is here purely to decrease the amount of clutter in the main file.

typedef struct StructMethodData {
    u32 name_loc;
    u32 name_len;
    u32 type;
    u32 data_loc;
} StructMethodData;

static u64 build_type_table(OnyxWasmModule* module) {

    bh_arr(u32) base_patch_locations=NULL;
    bh_arr_new(global_heap_allocator, base_patch_locations, 256);

#define PATCH (bh_arr_push(base_patch_locations, table_buffer.length))
#define WRITE_PTR(val) \
    bh_buffer_align(&table_buffer, POINTER_SIZE); \
    PATCH; \
    if (POINTER_SIZE == 4) bh_buffer_write_u32(&table_buffer, val); \
    if (POINTER_SIZE == 8) bh_buffer_write_u64(&table_buffer, val); 
#define WRITE_SLICE(ptr, count) \
    WRITE_PTR(ptr); \
    if (POINTER_SIZE == 4) bh_buffer_write_u32(&table_buffer, count); \
    if (POINTER_SIZE == 8) bh_buffer_write_u64(&table_buffer, count); 

    // This is the data behind the "type_table" slice in runtime/info/types.onyx
    #if (POINTER_SIZE == 4)
        #define Table_Info_Type u32
    #else
        #define Table_Info_Type u64
    #endif
    u32 type_count = bh_arr_length(type_map.entries) + 1;
    Table_Info_Type* table_info = bh_alloc_array(global_heap_allocator, Table_Info_Type, type_count); // HACK
    memset(table_info, 0, type_count * sizeof(Table_Info_Type));

    bh_buffer table_buffer;
    bh_buffer_init(&table_buffer, global_heap_allocator, 4096);

    u32 type_table_info_data_id = NEXT_DATA_ID(module);

    ConstExprContext constexpr_ctx;
    constexpr_ctx.module = module;
    constexpr_ctx.data_id = type_table_info_data_id;

    // Write a "NULL" at the beginning so nothing will have to point to the first byte of the buffer.
    bh_buffer_write_u64(&table_buffer, 0);

    bh_arr_each(bh__imap_entry, type_entry, type_map.entries) {
        u64 type_idx = type_entry->key;
        Type* type = (Type *) type_entry->value;

        switch (type->kind) {
            case Type_Kind_Basic: {
                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, type_size_of(type));
                bh_buffer_write_u32(&table_buffer, type_alignment_of(type));
                bh_buffer_write_u32(&table_buffer, type->Basic.kind);
                break;
            }

            case Type_Kind_Pointer: {
                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, type_size_of(type));
                bh_buffer_write_u32(&table_buffer, type_alignment_of(type));
                bh_buffer_write_u32(&table_buffer, type->Pointer.elem->id);
                break;
            }

            case Type_Kind_Array: {
                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, type_size_of(type));
                bh_buffer_write_u32(&table_buffer, type_alignment_of(type));
                bh_buffer_write_u32(&table_buffer, type->Array.elem->id);
                bh_buffer_write_u32(&table_buffer, type->Array.count);
                break;
            }

            case Type_Kind_Slice: {
                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, type_size_of(type));
                bh_buffer_write_u32(&table_buffer, type_alignment_of(type));
                bh_buffer_write_u32(&table_buffer, type->Slice.elem->id);
                break;
            }

            case Type_Kind_DynArray: {
                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, type_size_of(type));
                bh_buffer_write_u32(&table_buffer, type_alignment_of(type));
                bh_buffer_write_u32(&table_buffer, type->DynArray.elem->id);
                break;
            }

            case Type_Kind_VarArgs: {
                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, type_size_of(type));
                bh_buffer_write_u32(&table_buffer, type_alignment_of(type));
                bh_buffer_write_u32(&table_buffer, type->VarArgs.elem->id);
                break;
            }

            case Type_Kind_Compound: {
                u32 components_base = table_buffer.length;

                u32 components_count = type->Compound.count;
                fori (i, 0, components_count) {
                    u32 type_idx = type->Compound.types[i]->id;
                    bh_buffer_write_u32(&table_buffer, type_idx);
                }

                bh_buffer_align(&table_buffer, 8);
                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, type_size_of(type));
                bh_buffer_write_u32(&table_buffer, type_alignment_of(type));
                WRITE_SLICE(components_base, components_count);
                break;
            }

            case Type_Kind_Function: {
                u32 parameters_base = table_buffer.length;

                u32 parameters_count = type->Function.param_count;
                fori (i, 0, parameters_count) {
                    u32 type_idx = type->Function.params[i]->id;
                    bh_buffer_write_u32(&table_buffer, type_idx);
                }

                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, type_size_of(type));
                bh_buffer_write_u32(&table_buffer, type_alignment_of(type));
                bh_buffer_write_u32(&table_buffer, type->Function.return_type->id);

                WRITE_SLICE(parameters_base, parameters_count);

                bh_buffer_write_u32(&table_buffer, type->Function.vararg_arg_pos > 0 ? 1 : 0);
                break;
            }

            case Type_Kind_Enum: {
                AstEnumType* ast_enum = (AstEnumType *) type->ast_type;
                u32 member_count = bh_arr_length(ast_enum->values);
                u32* name_locations = bh_alloc_array(global_scratch_allocator, u32, member_count);

                u32 i = 0;
                bh_arr_each(AstEnumValue *, value, ast_enum->values) {
                    name_locations[i++] = table_buffer.length;

                    bh_buffer_append(&table_buffer, (*value)->token->text, (*value)->token->length);
                }
                bh_buffer_align(&table_buffer, 8);

                u32 member_base = table_buffer.length;
                i = 0;
                bh_arr_each(AstEnumValue *, value, ast_enum->values) {
                    u32 name_loc = name_locations[i++];

                    bh_buffer_align(&table_buffer, 8);
                    WRITE_SLICE(name_loc, (*value)->token->length);

                    assert((*value)->value->kind == Ast_Kind_NumLit);
                    AstNumLit *num = (AstNumLit *) (*value)->value;
                    bh_buffer_write_u64(&table_buffer, num->value.l);
                }

                u32 name_base = table_buffer.length;
                u32 name_length = strlen(type->Enum.name);
                bh_buffer_append(&table_buffer, type->Enum.name, name_length);
                bh_buffer_align(&table_buffer, 8);

                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, type_size_of(type));
                bh_buffer_write_u32(&table_buffer, type_alignment_of(type));
                bh_buffer_write_u32(&table_buffer, type->Enum.backing->id);
                WRITE_SLICE(name_base, name_length);
                WRITE_SLICE(member_base, member_count);
                bh_buffer_write_u32(&table_buffer, type->Enum.is_flags ? 1 : 0);
                break;
            }

            case Type_Kind_Struct: {
                TypeStruct* s = &type->Struct;
                u32* name_locations = bh_alloc_array(global_scratch_allocator, u32, s->mem_count);
                u32* param_locations = bh_alloc_array(global_scratch_allocator, u32, bh_arr_length(s->poly_sln));
                u32* value_locations = bh_alloc_array(global_scratch_allocator, u32, s->mem_count);
                u32* meta_locations = bh_alloc_array(global_scratch_allocator, u32, s->mem_count);
                u32* struct_tag_locations = bh_alloc_array(global_scratch_allocator, u32, bh_arr_length(s->meta_tags));
                memset(value_locations, 0, s->mem_count * sizeof(u32));
                memset(meta_locations, 0, s->mem_count * sizeof(u32));
                memset(struct_tag_locations, 0, bh_arr_length(s->meta_tags) * sizeof(u32));

                // Member names
                u32 i = 0;
                bh_arr_each(StructMember*, pmem, s->memarr) {
                    StructMember* mem = *pmem;

                    name_locations[i++] = table_buffer.length;
                    bh_buffer_append(&table_buffer, mem->name, strlen(mem->name));
                }

                bh_buffer_align(&table_buffer, 8);

                // Polymorphic solutions
                i = 0;
                bh_arr_each(AstPolySolution, sln, s->poly_sln) {
                    bh_buffer_align(&table_buffer, 8);
                    param_locations[i++] = table_buffer.length;

                    switch (sln->kind) {
                        case PSK_Type: {
                            // NOTE: This assumes a little endian compiler (which is assumed in other part of the code too)
                            bh_buffer_append(&table_buffer, &sln->type->id, 4);
                            break;
                        }

                        case PSK_Value: {
                            assert(sln->value->type);
                            u32 size = type_size_of(sln->value->type);

                            bh_buffer_grow(&table_buffer, table_buffer.length + size);
                            constexpr_ctx.data = table_buffer.data;
                            if (emit_constexpr_(&constexpr_ctx, sln->value, table_buffer.length)) {
                                table_buffer.length += size;
                                break;
                            }

                            // fallthrough
                        }

                        default: {
                            // Set to null if this is not known how to encode
                            param_locations[i-1] = 0;
                            break;
                        }
                    }
                }

                bh_buffer_align(&table_buffer, 8);

                // Member default values
                i = 0;
                bh_arr_each(StructMember*, pmem, s->memarr) {
                    StructMember* mem = *pmem;

                    if (mem->initial_value == NULL || *mem->initial_value == NULL) {
                        i++;
                        continue;
                    }

                    AstTyped* value = *mem->initial_value;
                    assert(value->type);

                    if ((value->flags & Ast_Flag_Comptime) == 0) {
                        // onyx_report_warning(value->token->pos, "Warning: skipping generating default value for '%s' in '%s' because it is not compile-time known.\n", mem->name, s->name);
                        i++;
                        continue;
                    }

                    u32 size = type_size_of(value->type);
                    bh_buffer_align(&table_buffer, type_alignment_of(value->type));

                    bh_buffer_grow(&table_buffer, table_buffer.length + size);
                    constexpr_ctx.data = table_buffer.data;
                    if (!emit_constexpr_(&constexpr_ctx, value, table_buffer.length)) {
                        // Failed to generate raw data
                        // onyx_report_warning(value->token->pos, "Warning: failed to generate default value for '%s' in '%s'.\n", mem->name, s->name);
                        value_locations[i++] = 0;

                    } else {
                        // Success 
                        value_locations[i++] = table_buffer.length;
                        table_buffer.length += size;
                    }
                }

                // Member tags
                i = 0;
                bh_arr_each(StructMember*, pmem, s->memarr) {
                    StructMember* mem = *pmem;

                    if (mem->meta_tags == NULL) {
                        i += 1;
                        continue;
                    }

                    bh_arr(AstTyped *) meta_tags = mem->meta_tags;
                    assert(meta_tags);

                    bh_arr(u64) meta_tag_locations=NULL;
                    bh_arr_new(global_heap_allocator, meta_tag_locations, bh_arr_length(meta_tags));

                    int j = 0;
                    bh_arr_each(AstTyped *, meta, meta_tags) {
                        AstTyped* value = *meta;                        
                        assert(value->flags & Ast_Flag_Comptime);
                        assert(value->type);

                        u32 size = type_size_of(value->type);
                        bh_buffer_align(&table_buffer, type_alignment_of(value->type));
                        meta_tag_locations[j] = table_buffer.length;

                        bh_buffer_grow(&table_buffer, table_buffer.length + size);
                        constexpr_ctx.data = table_buffer.data;
                        assert(emit_constexpr_(&constexpr_ctx, value, table_buffer.length));
                        table_buffer.length += size;

                        j += 1;
                    }

                    bh_buffer_align(&table_buffer, 8);
                    meta_locations[i] = table_buffer.length;

                    fori (k, 0, bh_arr_length(meta_tags)) {
                        WRITE_SLICE(meta_tag_locations[k], meta_tags[k]->type->id);
                    }

                    bh_arr_free(meta_tag_locations);
                    i += 1;
                }

                bh_buffer_align(&table_buffer, 8);
                u32 members_base = table_buffer.length;

                // Member array
                i = 0;
                bh_arr_each(StructMember*, pmem, s->memarr) {
                    StructMember* mem = *pmem;

                    u32 name_loc = name_locations[i];
                    u32 value_loc = value_locations[i];
                    u32 meta_loc = meta_locations[i++];

                    WRITE_SLICE(name_loc, strlen(mem->name));
                    bh_buffer_write_u32(&table_buffer, mem->offset);
                    bh_buffer_write_u32(&table_buffer, mem->type->id);
                    bh_buffer_write_byte(&table_buffer, mem->used ? 1 : 0);
                    
                    WRITE_PTR(value_loc);

                    WRITE_SLICE(meta_loc, bh_arr_length(mem->meta_tags));
                }

                bh_buffer_align(&table_buffer, 8);
                u32 params_base = table_buffer.length;

                // Polymorphic solution any array
                i = 0;
                bh_arr_each(AstPolySolution, sln, s->poly_sln) {
                    WRITE_PTR(param_locations[i++]);

                    if (sln->kind == PSK_Type) bh_buffer_write_u32(&table_buffer, basic_types[Basic_Kind_Type_Index].id);
                    else                       bh_buffer_write_u32(&table_buffer, sln->value->type->id);
                }

                // Struct tag array
                i = 0;
                bh_arr_each(AstTyped *, tag, s->meta_tags) {
                    AstTyped* value = *tag;                        
                    assert(value->flags & Ast_Flag_Comptime);
                    assert(value->type);

                    u32 size = type_size_of(value->type);
                    bh_buffer_align(&table_buffer, type_alignment_of(value->type));
                    struct_tag_locations[i] = table_buffer.length;

                    bh_buffer_grow(&table_buffer, table_buffer.length + size);
                    constexpr_ctx.data = table_buffer.data;
                    assert(emit_constexpr_(&constexpr_ctx, value, table_buffer.length));
                    table_buffer.length += size;

                    i += 1;
                }

                // Struct methods
                bh_arr(StructMethodData) method_data=NULL;
                AstType *ast_type = type->ast_type;
                if (ast_type && ast_type->kind == Ast_Kind_Struct_Type) {
                    AstStructType *struct_type  = (AstStructType *) ast_type;
                    Scope*         struct_scope = struct_type->scope;

                    if (struct_scope == NULL) goto no_methods;

                    fori (i, 0, shlen(struct_scope->symbols)) {
                        AstFunction* node = (AstFunction *) strip_aliases(struct_scope->symbols[i].value);
                        if (node->kind != Ast_Kind_Function) continue;
                        assert(node->entity);
                        assert(node->entity->function == node);

                        // Name
                        char *name = struct_scope->symbols[i].key;
                        u32 name_loc = table_buffer.length;
                        u32 name_len = strlen(name);
                        bh_buffer_append(&table_buffer, name, name_len);

                        // any data member
                        bh_buffer_align(&table_buffer, 4);
                        u32 data_loc = table_buffer.length;
                        u32 func_idx = get_element_idx(module, node);
                        bh_buffer_write_u32(&table_buffer, func_idx);
                        
                        bh_arr_push(method_data, ((StructMethodData) {
                            .name_loc = name_loc,
                            .name_len = name_len,
                            .type     = node->type->id,
                            .data_loc = data_loc,
                        }));
                    }
                }

                no_methods:

                bh_buffer_align(&table_buffer, 4);
                u32 method_data_base = table_buffer.length;

                i = 0;
                bh_arr_each(StructMethodData, method, method_data) {
                    WRITE_SLICE(method->name_loc, method->name_len);
                    WRITE_PTR(method->data_loc); 
                    bh_buffer_write_u32(&table_buffer, method->type);
                }

                bh_buffer_align(&table_buffer, 8);
                u32 struct_tag_base = table_buffer.length;

                fori (i, 0, bh_arr_length(s->meta_tags)) {
                    WRITE_SLICE(struct_tag_locations[i], s->meta_tags[i]->type->id);
                }

                // Struct name
                u32 name_base = 0;
                u32 name_length = 0;
                if (s->name) {
                    name_length = strlen(s->name);
                    name_base = table_buffer.length;
                    bh_buffer_append(&table_buffer, s->name, name_length);
                }

                bh_buffer_align(&table_buffer, 8);
                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, type_size_of(type));
                bh_buffer_write_u32(&table_buffer, type_alignment_of(type));

                if (type->Struct.constructed_from != NULL) {
                    bh_buffer_write_u32(&table_buffer, type->Struct.constructed_from->type_id);
                } else {
                    bh_buffer_write_u32(&table_buffer, 0);
                }

                WRITE_SLICE(name_base, name_length);
                WRITE_SLICE(members_base, s->mem_count);
                WRITE_SLICE(params_base, bh_arr_length(s->poly_sln));
                WRITE_SLICE(struct_tag_base, bh_arr_length(s->meta_tags));
                WRITE_SLICE(method_data_base, bh_arr_length(method_data));

                bh_arr_free(method_data);
                break;
            }

            case Type_Kind_PolyStruct: {
                u32* tag_locations = bh_alloc_array(global_scratch_allocator, u32, bh_arr_length(type->PolyStruct.meta_tags));
                memset(tag_locations, 0, sizeof(u32) * bh_arr_length(type->PolyStruct.meta_tags));

                u32 name_base = table_buffer.length;
                u32 name_length = strlen(type->PolyStruct.name);
                bh_buffer_append(&table_buffer, type->PolyStruct.name, name_length);

                u32 tags_count = bh_arr_length(type->PolyStruct.meta_tags);
                i32 i = 0;
                bh_arr_each(AstTyped *, tag, type->PolyStruct.meta_tags) {
                    AstTyped* value = *tag;                        

                    // Polymorphic structs are weird in this case, because the tag might not be constructed generically for
                    // the polymorphic structure so it should only be constructed for actual solidified structures.
                    // See core/containers/map.onyx with Custom_Format for an example.
                    if (!(value->flags & Ast_Flag_Comptime)) {
                        tags_count--;
                        continue;
                    }

                    assert(value->type);

                    u32 size = type_size_of(value->type);
                    bh_buffer_align(&table_buffer, type_alignment_of(value->type));
                    tag_locations[i] = table_buffer.length;

                    bh_buffer_grow(&table_buffer, table_buffer.length + size);

                    constexpr_ctx.data = table_buffer.data;
                    assert(emit_constexpr_(&constexpr_ctx, value, table_buffer.length));
                    table_buffer.length += size;

                    i += 1;
                }

                bh_buffer_align(&table_buffer, 8);
                u32 tags_base = table_buffer.length;

                fori (i, 0, tags_count) {
                    WRITE_SLICE(tag_locations[i], type->PolyStruct.meta_tags[i]->type->id);
                }

                bh_buffer_align(&table_buffer, 8);
                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, 0);
                bh_buffer_write_u32(&table_buffer, 0);
                WRITE_SLICE(name_base, name_length);
                WRITE_SLICE(tags_base, tags_count);

                break;
            }
        
            case Type_Kind_Distinct: {
                u32 name_base = table_buffer.length;
                u32 name_length = strlen(type->Distinct.name);
                bh_buffer_append(&table_buffer, type->Distinct.name, name_length);
                bh_buffer_align(&table_buffer, 8);

                table_info[type_idx] = table_buffer.length;
                bh_buffer_write_u32(&table_buffer, type->kind);
                bh_buffer_write_u32(&table_buffer, type_size_of(type));
                bh_buffer_write_u32(&table_buffer, type_alignment_of(type));
                bh_buffer_write_u32(&table_buffer, type->Distinct.base_type->id);
                WRITE_SLICE(name_base, name_length);
                break;
            }
        }
    }

    if (context.options->verbose_output == 1) {
        bh_printf("Type table size: %d bytes.\n", table_buffer.length);
    }

    WasmDatum type_info_data = {
        .alignment = 8,
        .length = table_buffer.length,
        .data = table_buffer.data,
    };
    emit_data_entry(module, &type_info_data);
    assert(type_info_data.id == type_table_info_data_id);

    bh_arr_each(u32, patch_loc, base_patch_locations) {
        DatumPatchInfo patch;
        patch.kind = Datum_Patch_Relative;
        patch.data_id = type_info_data.id;
        patch.offset = 0;
        patch.index = type_info_data.id;
        patch.location = *patch_loc;
        bh_arr_push(module->data_patches, patch);
    }

    WasmDatum type_table_data = {
        .alignment = POINTER_SIZE,
        .length = type_count * POINTER_SIZE,
        .data = table_info,
    };
    emit_data_entry(module, &type_table_data);

    fori (i, 0, type_count) {
        DatumPatchInfo patch;
        patch.kind = Datum_Patch_Data;
        patch.data_id = type_info_data.id;
        patch.offset = table_info[i];
        patch.index = type_table_data.id;
        patch.location = i * POINTER_SIZE;
        bh_arr_push(module->data_patches, patch);
    }

    Table_Info_Type* tmp_data = bh_alloc(global_heap_allocator, 2 * POINTER_SIZE);
    tmp_data[0] = 0;
    tmp_data[1] = type_count;
    WasmDatum type_table_global_data = {
        .alignment = POINTER_SIZE,
        .length = 2 * POINTER_SIZE,
        .data = tmp_data,
    };
    emit_data_entry(module, &type_table_global_data);

    {
        DatumPatchInfo patch;
        patch.kind = Datum_Patch_Data;
        patch.data_id = type_table_data.id;
        patch.offset = 0;
        patch.index = type_table_global_data.id;
        patch.location = 0;
        bh_arr_push(module->data_patches, patch);
    }

    return type_table_global_data.id;

#undef WRITE_SLICE
#undef WRITE_PTR
#undef PATCH
}



static u64 build_foreign_blocks(OnyxWasmModule* module) {
    bh_arr(u32) base_patch_locations=NULL;
    bh_arr_new(global_heap_allocator, base_patch_locations, 256);

#define PATCH (bh_arr_push(base_patch_locations, foreign_buffer.length))
#define WRITE_PTR(val) \
    bh_buffer_align(&foreign_buffer, POINTER_SIZE); \
    PATCH; \
    if (POINTER_SIZE == 4) bh_buffer_write_u32(&foreign_buffer, val); \
    if (POINTER_SIZE == 8) bh_buffer_write_u64(&foreign_buffer, val); 
#define WRITE_SLICE(ptr, count) \
    WRITE_PTR(ptr); \
    if (POINTER_SIZE == 4) bh_buffer_write_u32(&foreign_buffer, count); \
    if (POINTER_SIZE == 8) bh_buffer_write_u64(&foreign_buffer, count); 

    // This is the data behind the "type_table" slice in type_info.onyx
    #if (POINTER_SIZE == 4)
        #define Foreign_Block_Type u32
    #else
        #define Foreign_Block_Type u64
    #endif
    u32 block_count = bh_arr_length(module->foreign_blocks);
    Foreign_Block_Type* foreign_info = bh_alloc_array(global_heap_allocator, Foreign_Block_Type, block_count); // HACK
    memset(foreign_info, 0, block_count * sizeof(Foreign_Block_Type));

    bh_buffer foreign_buffer;
    bh_buffer_init(&foreign_buffer, global_heap_allocator, 4096);

    // 
    // This is necessary because 0 is an invalid offset to store in this
    // buffer, as 0 will map to NULL. This could be a single byte insertion,
    // but 64 bytes keeps better alignment.
    bh_buffer_write_u64(&foreign_buffer, 0);

    u32 index = 0;
    bh_arr_each(AstForeignBlock *, pfb, module->foreign_blocks) {
        AstForeignBlock *fb = *pfb;

        u32 funcs_length = 0;

        u32 *name_offsets = bh_alloc_array(global_scratch_allocator, u32, shlen(fb->scope->symbols));
        u32 *name_lengths = bh_alloc_array(global_scratch_allocator, u32, shlen(fb->scope->symbols));
        u32 *func_types   = bh_alloc_array(global_scratch_allocator, u32, shlen(fb->scope->symbols));

        fori (i, 0, shlen(fb->scope->symbols)) {
            AstFunction *func = (AstFunction *) fb->scope->symbols[i].value;
            if (func->kind != Ast_Kind_Function) continue;

            u32 func_name_base = foreign_buffer.length;
            u32 func_name_length = func->foreign_name->length;
            bh_buffer_append(&foreign_buffer, func->foreign_name->text, func_name_length);

            name_offsets[funcs_length] = func_name_base;
            name_lengths[funcs_length] = func_name_length;
            func_types[funcs_length]   = func->type->id;
            funcs_length++;
        }

        bh_buffer_align(&foreign_buffer, 8);
        u32 funcs_base = foreign_buffer.length;

        fori (i, 0, (i64) funcs_length) {
            bh_buffer_align(&foreign_buffer, POINTER_SIZE);
            WRITE_SLICE(name_offsets[i], name_lengths[i]);
            bh_buffer_write_u32(&foreign_buffer, func_types[i]);
        }

        u32 name_base = foreign_buffer.length;
        u32 name_length = fb->module_name->length;
        bh_buffer_append(&foreign_buffer, fb->module_name->text, name_length);
        bh_buffer_align(&foreign_buffer, 8);

        foreign_info[index] = foreign_buffer.length;
        WRITE_SLICE(name_base, name_length);
        WRITE_SLICE(funcs_base, funcs_length);
        index++;
    }


    if (context.options->verbose_output == 1) {
        bh_printf("Foreign blocks size: %d bytes.\n", foreign_buffer.length);
    }

    WasmDatum foreign_info_data = {
        .alignment = 8,
        .length = foreign_buffer.length,
        .data = foreign_buffer.data,
    };
    emit_data_entry(module, &foreign_info_data);

    bh_arr_each(u32, patch_loc, base_patch_locations) {
        DatumPatchInfo patch;
        patch.kind = Datum_Patch_Relative;
        patch.data_id = foreign_info_data.id;
        patch.offset = 0;
        patch.index = foreign_info_data.id;
        patch.location = *patch_loc;
        bh_arr_push(module->data_patches, patch);
    }

    WasmDatum foreign_table_data = {
        .alignment = POINTER_SIZE,
        .length = block_count * POINTER_SIZE,
        .data = foreign_info,
    };
    emit_data_entry(module, &foreign_table_data);

    fori (i, 0, block_count) {
        DatumPatchInfo patch;
        patch.kind = Datum_Patch_Data;
        patch.data_id = foreign_info_data.id;
        patch.offset = foreign_info[i];
        patch.index = foreign_table_data.id;
        patch.location = i * POINTER_SIZE;
        bh_arr_push(module->data_patches, patch);
    }

    Foreign_Block_Type* tmp_data = bh_alloc(global_heap_allocator, 2 * POINTER_SIZE);
    tmp_data[0] = 0;
    tmp_data[1] = block_count;
    WasmDatum foreign_table_global_data = {
        .alignment = POINTER_SIZE,
        .length = 2 * POINTER_SIZE,
        .data = tmp_data,
    };
    emit_data_entry(module, &foreign_table_global_data);

    {
        DatumPatchInfo patch;
        patch.kind = Datum_Patch_Data;
        patch.data_id = foreign_table_data.id;
        patch.location = 0;
        patch.index = foreign_table_global_data.id;
        patch.offset = 0;
        bh_arr_push(module->data_patches, patch);
    }

    return foreign_table_global_data.id;

#undef WRITE_SLICE
#undef WRITE_PTR
#undef PATCH
}



static u64 build_tagged_procedures(OnyxWasmModule *module) {
    bh_arr(u32) base_patch_locations=NULL;
    bh_arr_new(global_heap_allocator, base_patch_locations, 256);

#define PATCH (bh_arr_push(base_patch_locations, tag_proc_buffer.length))
#define WRITE_PTR(val) \
    bh_buffer_align(&tag_proc_buffer, POINTER_SIZE); \
    PATCH; \
    if (POINTER_SIZE == 4) bh_buffer_write_u32(&tag_proc_buffer, val); \
    if (POINTER_SIZE == 8) bh_buffer_write_u64(&tag_proc_buffer, val); 
#define WRITE_SLICE(ptr, count) \
    WRITE_PTR(ptr); \
    if (POINTER_SIZE == 4) bh_buffer_write_u32(&tag_proc_buffer, count); \
    if (POINTER_SIZE == 8) bh_buffer_write_u64(&tag_proc_buffer, count); 

    #if (POINTER_SIZE == 4)
        #define Tagged_Procedure_Type u32
    #else
        #define Tagged_Procedure_Type u64
    #endif
    u32 proc_count = bh_arr_length(module->procedures_with_tags);
    Tagged_Procedure_Type* tag_proc_info = bh_alloc_array(global_heap_allocator, Tagged_Procedure_Type, proc_count); // HACK
    memset(tag_proc_info, 0, proc_count * sizeof(Tagged_Procedure_Type));

    bh_buffer tag_proc_buffer;
    bh_buffer_init(&tag_proc_buffer, global_heap_allocator, 4096);

    u32 proc_info_data_id = NEXT_DATA_ID(module);

    ConstExprContext constexpr_ctx;
    constexpr_ctx.module = module;
    constexpr_ctx.data_id = proc_info_data_id;

    // 
    // This is necessary because 0 is an invalid offset to store in this
    // buffer, as 0 will map to NULL. This could be a single byte insertion,
    // but 64 bytes keeps better alignment.
    bh_buffer_write_u64(&tag_proc_buffer, 0);

    u32 index = 0;
    bh_arr_each(AstFunction *, pfunc, module->procedures_with_tags) {
        AstFunction *func = *pfunc;
        if (!should_emit_function(func)) {
            proc_count--;
            continue;
        }

        u32 tag_count = bh_arr_length(func->tags);
        u32 *tag_data_offsets = bh_alloc_array(global_scratch_allocator, u32, tag_count);
        u32 *tag_data_types   = bh_alloc_array(global_scratch_allocator, u32, tag_count);

        u32 tag_index = 0;
        bh_arr_each(AstTyped *, ptag, func->tags) {
            AstTyped *tag = *ptag;
            bh_buffer_align(&tag_proc_buffer, type_alignment_of(tag->type));

            tag_data_offsets[tag_index  ] = tag_proc_buffer.length;
            tag_data_types  [tag_index++] = tag->type->id;

            u32 size = type_size_of(tag->type);
            bh_buffer_grow(&tag_proc_buffer, tag_proc_buffer.length + size);

            constexpr_ctx.data = tag_proc_buffer.data;
            emit_constexpr(&constexpr_ctx, tag, tag_proc_buffer.length);
            tag_proc_buffer.length += size;
        }

        bh_buffer_align(&tag_proc_buffer, 4);
        u32 tag_array_base = tag_proc_buffer.length;
        fori (i, 0, tag_count) {
            PATCH;
            bh_buffer_write_u32(&tag_proc_buffer, tag_data_offsets[i]);
            bh_buffer_write_u32(&tag_proc_buffer, tag_data_types[i]);
        }

        bh_buffer_align(&tag_proc_buffer, 4);
        tag_proc_info[index++] = tag_proc_buffer.length;

        assert(func->entity && func->entity->package);

        bh_buffer_write_u32(&tag_proc_buffer, get_element_idx(module, func));
        bh_buffer_write_u32(&tag_proc_buffer, func->type->id);
        WRITE_SLICE(tag_array_base, tag_count);
        bh_buffer_write_u32(&tag_proc_buffer, func->entity->package->id);
    }    

    if (context.options->verbose_output == 1) {
        bh_printf("Tagged procedure size: %d bytes.\n", tag_proc_buffer.length);
    }

    WasmDatum proc_info_data = {
        .alignment = 8,
        .length = tag_proc_buffer.length,
        .data = tag_proc_buffer.data,
    };
    emit_data_entry(module, &proc_info_data);
    assert(proc_info_data.id == proc_info_data_id);

    bh_arr_each(u32, patch_loc, base_patch_locations) {
        DatumPatchInfo patch;
        patch.kind = Datum_Patch_Relative;
        patch.data_id = proc_info_data.id;
        patch.location = *patch_loc;
        patch.index = proc_info_data.id;
        patch.offset = 0;
        bh_arr_push(module->data_patches, patch);
    }

    WasmDatum proc_table_data = {
        .alignment = POINTER_SIZE,
        .length = proc_count * POINTER_SIZE,
        .data = tag_proc_info,
    };
    emit_data_entry(module, &proc_table_data);

    fori (i, 0, proc_count) {
        DatumPatchInfo patch;
        patch.kind = Datum_Patch_Data;
        patch.data_id = proc_info_data.id;
        patch.offset = tag_proc_info[i];
        patch.index = proc_table_data.id;
        patch.location = i * POINTER_SIZE;
        bh_arr_push(module->data_patches, patch);
    }

    Tagged_Procedure_Type* tmp_data = bh_alloc(global_heap_allocator, 2 * POINTER_SIZE);
    tmp_data[0] = 0;
    tmp_data[1] = proc_count;
    WasmDatum proc_table_global_data = {
        .alignment = POINTER_SIZE,
        .length = 2 * POINTER_SIZE,
        .data = tmp_data,
    };
    emit_data_entry(module, &proc_table_global_data);

    {
        DatumPatchInfo patch;
        patch.kind = Datum_Patch_Data;
        patch.offset = 0;
        patch.data_id = proc_table_data.id;
        patch.index = proc_table_global_data.id;
        patch.location = 0;
        bh_arr_push(module->data_patches, patch);
    }

    return proc_table_global_data.id;

#undef WRITE_SLICE
#undef WRITE_PTR
#undef PATCH
}

