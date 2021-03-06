//
// Copyright (c) 2017 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// IntermNode_util.cpp: High-level utilities for creating AST nodes and node hierarchies. Mostly
// meant to be used in AST transforms.

#include "compiler/translator/tree_util/IntermNode_util.h"

#include "compiler/translator/FunctionLookup.h"
#include "compiler/translator/SymbolTable.h"

namespace sh
{

namespace
{

const TFunction *LookUpBuiltInFunction(const char *name,
                                       const TIntermSequence *arguments,
                                       const TSymbolTable &symbolTable,
                                       int shaderVersion)
{
    const ImmutableString &mangledName = TFunctionLookup::GetMangledName(name, *arguments);
    const TSymbol *symbol              = symbolTable.findBuiltIn(mangledName, shaderVersion);
    if (symbol)
    {
        ASSERT(symbol->isFunction());
        return static_cast<const TFunction *>(symbol);
    }
    return nullptr;
}

}  // anonymous namespace

TIntermFunctionPrototype *CreateInternalFunctionPrototypeNode(const TFunction &func)
{
    return new TIntermFunctionPrototype(&func);
}

TIntermFunctionDefinition *CreateInternalFunctionDefinitionNode(const TFunction &func,
                                                                TIntermBlock *functionBody)
{
    return new TIntermFunctionDefinition(new TIntermFunctionPrototype(&func), functionBody);
}

TIntermTyped *CreateZeroNode(const TType &type)
{
    TType constType(type);
    constType.setQualifier(EvqConst);

    if (!type.isArray() && type.getBasicType() != EbtStruct)
    {
        size_t size       = constType.getObjectSize();
        TConstantUnion *u = new TConstantUnion[size];
        for (size_t i = 0; i < size; ++i)
        {
            switch (type.getBasicType())
            {
                case EbtFloat:
                    u[i].setFConst(0.0f);
                    break;
                case EbtInt:
                    u[i].setIConst(0);
                    break;
                case EbtUInt:
                    u[i].setUConst(0u);
                    break;
                case EbtBool:
                    u[i].setBConst(false);
                    break;
                default:
                    // CreateZeroNode is called by ParseContext that keeps parsing even when an
                    // error occurs, so it is possible for CreateZeroNode to be called with
                    // non-basic types. This happens only on error condition but CreateZeroNode
                    // needs to return a value with the correct type to continue the typecheck.
                    // That's why we handle non-basic type by setting whatever value, we just need
                    // the type to be right.
                    u[i].setIConst(42);
                    break;
            }
        }

        TIntermConstantUnion *node = new TIntermConstantUnion(u, constType);
        return node;
    }

    if (type.getBasicType() == EbtVoid)
    {
        // Void array. This happens only on error condition, similarly to the case above. We don't
        // have a constructor operator for void, so this needs special handling. We'll end up with a
        // value without the array type, but that should not be a problem.
        while (constType.isArray())
        {
            constType.toArrayElementType();
        }
        return CreateZeroNode(constType);
    }

    TIntermSequence *arguments = new TIntermSequence();

    if (type.isArray())
    {
        TType elementType(type);
        elementType.toArrayElementType();

        size_t arraySize = type.getOutermostArraySize();
        for (size_t i = 0; i < arraySize; ++i)
        {
            arguments->push_back(CreateZeroNode(elementType));
        }
    }
    else
    {
        ASSERT(type.getBasicType() == EbtStruct);

        const TStructure *structure = type.getStruct();
        for (const auto &field : structure->fields())
        {
            arguments->push_back(CreateZeroNode(*field->type()));
        }
    }

    return TIntermAggregate::CreateConstructor(constType, arguments);
}

TIntermConstantUnion *CreateIndexNode(int index)
{
    TConstantUnion *u = new TConstantUnion[1];
    u[0].setIConst(index);

    TType type(EbtInt, EbpUndefined, EvqConst, 1);
    TIntermConstantUnion *node = new TIntermConstantUnion(u, type);
    return node;
}

TIntermConstantUnion *CreateBoolNode(bool value)
{
    TConstantUnion *u = new TConstantUnion[1];
    u[0].setBConst(value);

    TType type(EbtBool, EbpUndefined, EvqConst, 1);
    TIntermConstantUnion *node = new TIntermConstantUnion(u, type);
    return node;
}

TVariable *CreateTempVariable(TSymbolTable *symbolTable, const TType *type)
{
    ASSERT(symbolTable != nullptr);
    // TODO(oetuaho): Might be useful to sanitize layout qualifier etc. on the type of the created
    // variable. This might need to be done in other places as well.
    return new TVariable(symbolTable, ImmutableString(""), type, SymbolType::AngleInternal);
}

TVariable *CreateTempVariable(TSymbolTable *symbolTable, const TType *type, TQualifier qualifier)
{
    ASSERT(symbolTable != nullptr);
    if (type->getQualifier() == qualifier)
    {
        return CreateTempVariable(symbolTable, type);
    }
    TType *typeWithQualifier = new TType(*type);
    typeWithQualifier->setQualifier(qualifier);
    return CreateTempVariable(symbolTable, typeWithQualifier);
}

TIntermSymbol *CreateTempSymbolNode(const TVariable *tempVariable)
{
    ASSERT(tempVariable->symbolType() == SymbolType::AngleInternal);
    ASSERT(tempVariable->getType().getQualifier() == EvqTemporary ||
           tempVariable->getType().getQualifier() == EvqConst ||
           tempVariable->getType().getQualifier() == EvqGlobal);
    return new TIntermSymbol(tempVariable);
}

TIntermDeclaration *CreateTempDeclarationNode(const TVariable *tempVariable)
{
    TIntermDeclaration *tempDeclaration = new TIntermDeclaration();
    tempDeclaration->appendDeclarator(CreateTempSymbolNode(tempVariable));
    return tempDeclaration;
}

TIntermDeclaration *CreateTempInitDeclarationNode(const TVariable *tempVariable,
                                                  TIntermTyped *initializer)
{
    ASSERT(initializer != nullptr);
    TIntermSymbol *tempSymbol           = CreateTempSymbolNode(tempVariable);
    TIntermDeclaration *tempDeclaration = new TIntermDeclaration();
    TIntermBinary *tempInit             = new TIntermBinary(EOpInitialize, tempSymbol, initializer);
    tempDeclaration->appendDeclarator(tempInit);
    return tempDeclaration;
}

TIntermBinary *CreateTempAssignmentNode(const TVariable *tempVariable, TIntermTyped *rightNode)
{
    ASSERT(rightNode != nullptr);
    TIntermSymbol *tempSymbol = CreateTempSymbolNode(tempVariable);
    return new TIntermBinary(EOpAssign, tempSymbol, rightNode);
}

TVariable *DeclareTempVariable(TSymbolTable *symbolTable,
                               const TType *type,
                               TQualifier qualifier,
                               TIntermDeclaration **declarationOut)
{
    TVariable *variable = CreateTempVariable(symbolTable, type, qualifier);
    *declarationOut     = CreateTempDeclarationNode(variable);
    return variable;
}

TVariable *DeclareTempVariable(TSymbolTable *symbolTable,
                               TIntermTyped *initializer,
                               TQualifier qualifier,
                               TIntermDeclaration **declarationOut)
{
    TVariable *variable =
        CreateTempVariable(symbolTable, new TType(initializer->getType()), qualifier);
    *declarationOut = CreateTempInitDeclarationNode(variable, initializer);
    return variable;
}

TIntermBlock *EnsureBlock(TIntermNode *node)
{
    if (node == nullptr)
        return nullptr;
    TIntermBlock *blockNode = node->getAsBlock();
    if (blockNode != nullptr)
        return blockNode;

    blockNode = new TIntermBlock();
    blockNode->setLine(node->getLine());
    blockNode->appendStatement(node);
    return blockNode;
}

TIntermSymbol *ReferenceGlobalVariable(const ImmutableString &name, const TSymbolTable &symbolTable)
{
    const TVariable *var = reinterpret_cast<const TVariable *>(symbolTable.findGlobal(name));
    ASSERT(var);
    return new TIntermSymbol(var);
}

TIntermSymbol *ReferenceBuiltInVariable(const ImmutableString &name,
                                        const TSymbolTable &symbolTable,
                                        int shaderVersion)
{
    const TVariable *var =
        reinterpret_cast<const TVariable *>(symbolTable.findBuiltIn(name, shaderVersion));
    ASSERT(var);
    return new TIntermSymbol(var);
}

TIntermTyped *CreateBuiltInFunctionCallNode(const char *name,
                                            TIntermSequence *arguments,
                                            const TSymbolTable &symbolTable,
                                            int shaderVersion)
{
    const TFunction *fn = LookUpBuiltInFunction(name, arguments, symbolTable, shaderVersion);
    ASSERT(fn);
    TOperator op = fn->getBuiltInOp();
    if (op != EOpCallBuiltInFunction && arguments->size() == 1)
    {
        return new TIntermUnary(op, arguments->at(0)->getAsTyped(), fn);
    }
    return TIntermAggregate::CreateBuiltInFunctionCall(*fn, arguments);
}

}  // namespace sh
